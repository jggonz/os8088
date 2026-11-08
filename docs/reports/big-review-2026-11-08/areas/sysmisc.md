# Control Panel, clock, screen blanker

Area key: `sysmisc`. Reviewer read the code first and SPEC second; every claim
below carries a `file:line`. Build arm is named wherever a line belongs to one.

## Scope read

| file | lines | read |
|---|---|---|
| `kernel/ctrl.inc` | 7,870 | all of it: resident data (.text), the `.cold` gates and thunks, the `.modc` core, every `.modu` page |
| `kernel/clock.inc` | 2,349 | all: the `.ovl` RTC ladder, the `.text` software clock and formatters, `.bss`, `osapi_clk_get_x` |
| `kernel/clockw.inc` | 652 | all (CTRL.DRV's RTC write half) |
| `kernel/blank.inc` | 994 | all (blanker, SAVER.DRV glue, `blk_relit`) |
| SPEC.md | | §31 intro, 31.1-31.4, 31.8, 31.1.4; §37 intro, 37.0.1, 37.96; §64 whole; §76.4-76.5; §79.1-79.10 |
| context | | `kernel/ui.inc` 100-330 and 1060-1190 (key/wake/idle paths), `kernel/driver.inc` 880-1010 and 4950-5010 (SYSTEM.CFG map/unpack), `kernel/hiber.inc` 3110-3135 and 4620-4680 (clock catch-up), `kernel/fsx.inc` 490-530, `apps/dos/dos.asm` 13878-14135 (kerndos time), `drivers/saver/svcfg.inc` 680-750 |

Also checked: `docs/plans/ABI-BREAK-LIST.md`, `docs/plans/KERNEL-BYTE-QUEUE.md`, `docs/reports/CODE-REVIEW-TWO-CYCLES-2026-11-01.md`, and a grep of `docs/plans/` for clock, blanker and panel topics. None of the findings below is already listed there.

## Verdict

This area is in good shape structurally. The Control Panel is an on-demand module (CTRL.DRV, about 11 KB on kern_big, SPEC.md 2.8) behind small resident gates. The RTC ladder runs in the boot overlay and its write half ships in the module, so most of what this area does costs no resident memory. The blanker gates the video signal instead of drawing, and the animated saver falls back to that blank on every failure.

The code is mostly correct. One defect is significant: the clock runs about 31 seconds a day fast, because `clk_tick` divides by 18.2 where the true rate is 18.2065. The fix costs 0 bytes.

Two other defects are real but narrow:
- A hostile `SYSTEM.CFG` byte can make the Date/Time page step a kernel `.bss` byte 254 bytes past `clk_sec`.
- The time the user sets never reaches the BIOS tick count, so the kernel's own DOS shows a different time and date from the menu bar on an XT.

The biggest weakness is drift between the prose and the code. The Control Panel's SPEC section and file header still describe a one-page, 140-pixel, mouse-only panel with no `.bss`, a 32-byte settings file and a caption-reported save error. The clock prose still describes a month-length table, an RTC write drained by the UI loop, and display settings that are never persisted. About thirty such statements are listed below.

The two rethinks worth the owner's time:
- R1: derive the time of day from the BIOS count at 40:6C, as DOS does. That is one clock for the kernel and its DOS, no drift, and no 59-minute ceiling.
- R2: drive the static pages from declarative tables, the way the Mac's dialog item lists and the Windows dialog templates did. The estimate is 1.5-3 KB off CTRL.DRV and 0 resident bytes.

## Strengths

- **The RTC ladder is ordered so that no rung writes to a chip a later rung might have identified differently** (`clock.inc:300-394`, comment block at `:266-298`). Read-only probes run first. The RP5C01 is claimed only if it agrees with the BIOS on hour, day, month and year (`:1455-1494`). The MM58167 probe has a structural veto against a 4-bit part before its single scratch write (`:1006-1019`). DOS needed a separate `ASTCLOCK.COM` to see a SixPakPlus; this kernel finds it with no driver. Copy this discipline to any future hardware probe.
- **`clk_at_read` uses the hybrid guard**: the update-in-progress (UIP) bit plus a seconds compare on both sides of the burst (`clock.inc:539-605`), with the DM-bit fallback. This is the shape Linux converged on.
- **The write half obeys the machine's own conventions**:
  - DM and 24/12 are read and never forced (`clockw.inc:185-189`).
  - The RP5C01's alarm-enable bit survives all three MODE writes (`clockw.inc:441-446`).
  - The century byte is written back only where the read found one (`clockw.inc:241-250`).
- **The snapshot discipline** is cheap and sound. Every carrying write is one critical section (`clock.inc:1743-1805`, `ctrl.inc:5172-5254`), and readers format from `clk_snapshot` (`clock.inc:1851-1866`). An hour of held-open menu does not hold interrupts off for 3,600 seconds.
- **The resident gate before the module** (`cp_tick_due_x`, `ctrl.inc:1229-1247`; `cpf_cp_tick`, `:7790-7796`). The once-a-second question is answered from resident bytes, so CTRL.DRV is never read off a floppy to find out that nothing is showing. The same gate pattern makes `cpf_cp_flush_close` skip the load when `[cp_wdirty]` is clear (`:7765-7781`).
- **One page ladder serves as both hit test and action** (`cp_probe` 0/1/2, `ctrl.inc:440-449`, `cp_ctl` `:2099-2113`). This avoids a second copy of every page's geometry. The id-0 "selecting site" species keeps safe prefix actions on the press, as SPEC.md 13.6 requires.
- **Records and ordinals are separated** in the item list (`cp_v2r`/`cp_r2v`, `ctrl.inc:1771-1890`). A hidden row can sit anywhere in the table, and `[cp_sel]` names something durable.
- **The deferred `SYSTEM.CFG` write** (`[cp_wdirty]`, `ctrl.inc:349-370`; `cp_flush_cfg` `:5427-5436`). A setting's effect happens on the click; only the record waits for the close. A failed write keeps the flag set so the next close retries. On a machine where one write is about 2 s of frozen UI, this is right.
- **The blanker gates the video signal and leaves the framebuffer alone** (`blank.inc:27-36`, SPEC.md 64.3). Wake is instant and exact, no drawing path carries a test, and a background task may keep drawing. Windows 3.x savers paint black and pay a full repaint on wake; this does not.
- **The wake input is consumed** (`blk_wake`, `blank.inc:250-271`). `blk_pass` sits ahead of `evq_pop` and drains the ring record by record (`:340-371`), so `EVT_WAKE` records are owed rather than lost.
- **SAVER.DRV reuses the driver loader through a row-shaped record** (`ss_row`, `blank.inc:196-208`). It gets the mount, the claim, the check and the failure tail with no second loader. The overlay never volunteers to be freed, and `ss_reap_x` asks it whether it is busy (`:794-822`), which rules out freeing code that still has to return.
- **`WF_QUIET` picks which of the two answers a session gets, never whether there is one** (`blank.inc:925-946`, SPEC.md 79.10). A program that forgets to clear it leaves the monitor dark, not lit. This is the correct inversion of the Windows `SC_SCREENSAVE` veto.
- **Mirrored constants are gated.** `SSV_*`, `SS_M_*` and `SS_MAXMIN` are checked by `tests/unit/t_mirror.py` (`blank.inc:68-75`, `svcfg.inc:741-748`). `SS_IDLE_AT` is asserted to equal `SS_P_IDLE` (`blank.inc:161-165`).
- **The domain of `clk_barq` is written down where the trick is** (`clock.inc:2197-2213`): {0,1}, because `clk_tick` doubles the byte into mask bit 1.

## Findings

Ranked by value. *Phase*: `now` means no ABI break; `flagday` means it needs one; `record` means worth writing down but not recommended. *Bytes* are signed and say where they land: resident (`.text`/`.cold`/`.bss`), CTRL.DRV (a module, a heap claim only while the panel is open), `.ovl` (the boot overlay, freed after boot), or SAVER.DRV.

### sysmisc-1 - The system clock gains about 31 seconds a day (bug, medium, now, 0 bytes)

`clk_tick` converts ticks to seconds as `delta * 10 / 182` (`clock.inc:1686-1692`), which assumes 18.2 Hz. `[ticks]` runs at 1,193,182 / 65,536 = 18.20648 Hz (SPEC.md 64.1 says so; the BIOS day is 1,573,040 ticks). Each counted second is therefore 0.99964 s of wall time. That is +0.0356%, about **+30.8 s/day** or 3.6 min/week.

A machine with no RTC has no correction at all. An AT is corrected only at the next boot, because the RTC is never re-read (SPEC.md 37, "no re-reading of the RTC after boot"). The comment that calls this "what makes 18.2 Hz add up to real seconds" (`clock.inc:1663`, SPEC.md 37 *Advancing*) is the defect stated as a feature.

**Fix**, at the same encoding size: `mov dx, 625` / `mov cx, 11379`. 11379 / 625 = 18.20640, an error of 4.5e-6, about 0.4 s/day.
- Overflow: 65,535 × 625 + 11,378 < 2^32, and the quotient is at most 3,600, so the DIV cannot fault.
- Hibernation: the catch-up feeds 32,768-tick passes (`hiber.inc:4652-4676`), which stay in range.
- Comments to update: `[clk_acc]` becomes a 1/625-tick remainder (`clock.inc:2217`), and the "<= 3602" note (`:1665`) changes.

Related: `apps/timer/timer.asm:208` uses the same 182 for the Timer's stopwatch and reads 0.036% fast. `apps/dos/dos.asm:14120` uses 182 only within a minute, anchored on 32,772 half-ticks an hour, so its error does not accumulate.

Prior art: DOS's clock device derives the time of day from the BIOS count and so inherits the BIOS's exact day.

### sysmisc-2 - A hostile `SYSTEM.CFG` 'CH' byte lets the Date/Time page write past the clock fields (bug, medium, now, +5 `.ovl` and +5 CTRL.DRV, 0 resident)

The settings unpack copies each mapped byte raw into its kernel byte (`driver.inc:4964-4972`; `CFG_H12` maps to `clk_h12` at `driver.inc:906-907`). No domain check is applied. Every reader of `clk_h12` outside the Date/Time page tests for zero, so a 2 is just "12-hour" there (`clock.inc:1897-1904`, `:2343-2344` masks bit 0). The Date/Time page instead uses the byte as a COUNT:

- `cp_time_rows`: `mov ah, 6 / add ah, [clk_h12]` draws fields `0..5+h12` (`ctrl.inc:4859-4866`). A value of 2 draws an eighth field whose `{x, w, y}` come from the 4 bytes past `cp_tflds_end` (`:4876-4884`), which are code. `clk_fld_str(7)` then reads `[clk_sn_sec - 2]`.
- `cp_time_pick`: `mov bl, 5 / add bl, [clk_h12]` can hit-test that phantom field (`:4984-4987`), so `[cp_tsel]` becomes 7.
- `clk_fld_adj(7)` then computes `bl = 5 - 7 = 0FEh`, reads its range from `[cs: 0x1FC + clk_frng]`, and **steps the byte at `clk_sec + 254`** (`ctrl.inc:5190-5219`). That is kernel `.bss` corruption, reachable from a floppy and a click.

`xor byte [clk_h12], 1` does not repair the value (2 becomes 3, `ctrl.inc:4761`). A value of 0FFh is benign but hides the seconds field (6 + 255 = 5).

**Fix**: normalise at the unpack with `and byte [clk_h12], 1` after the map loop. The parser is assembled into the boot overlay and into CTRL.DRV (`driver.inc:887-893`), so this costs 5 bytes in each and 0 resident. The general rule is "every byte read off one is hostile". A bit-per-key mask in `CFG_MAP` would cover `clk_secs` and `wm_animon` too, but those are only ever tested for zero.

### sysmisc-3 - The time the user sets does not reach the BIOS tick count, so the kernel's DOS disagrees with the menu bar (bug, medium, now, about +35 CTRL.DRV, 0 resident)

`clock.inc:4` promises "one date + time of day for the whole system". kerndos takes the time of day from 0040:006C (`dos_ticks`/`dos_time_now`, `dos.asm:13878-13887`, `14098-14135`) and the date from `int 1Ah AH=04h` with the `CLK_DEF_*` fallback (`dos_date_init`, `dos.asm:13895-13906`). Nothing in the kernel writes 40:6C (a grep finds only reads in `hiber.inc`, `bootprof.inc` and `mega.inc`'s PM maintenance). `clk_rtc_write` is the only push outward.

Two consequences:
- On a 5150 with no clock card, a time set in the Control Panel is invisible to every DOS program: DOS's `TIME` reads time since power-on and its `DATE` reads 4 July 2026.
- On an AT, DOS's time of day stays at the boot value plus uptime until the next boot, because the BIOS seeds 40:6C from the RTC only at POST.

**Fix**: in `cp_flush_close_x`, beside the RTC write (`ctrl.inc:5486-5498`), set the BIOS count with `int 1Ah AH=01h` (implemented by every PC BIOS including the 5150's) from h:m:s × 18.2065. Do it when `[clk_dirty]` is set, regardless of `[clk_rtc]`. Clear 40:70's midnight flag on the way. About 35 bytes of 32-bit arithmetic, all in CTRL.DRV.

The date half belongs to the DOS area: kerndos is an os8088 package and could ask `OSAPI_CLK_GET` (SPEC.md 37.96) instead of `int 1Ah AH=04h`. R1 below removes the problem by construction.

### sysmisc-4 - The MM58167 New-Year shadow is refreshed only when the year is stamped, not on each boot (hazard, low, now, about +8 `.ovl`)

The MM58167 has no year register, so the year is reconstructed from RAM bytes, with a last-month shadow at 09h to detect a New Year that passed while the machine was off (`clk_ns_year`/`clk_ns_roll`, `clock.inc:1160-1218`). `clk_ns_read` writes the shadow back only when a roll fired (`clock.inc:1090-1105`); otherwise it is written only by a Set (`clockw.inc:322-327`).

Worked example: set in March 2026, so the shadow is 3. Booted in December 2026: 12 > 3, no roll, correct. Off until April 2027: 4 is not below the shadow 3, so there is no roll and the year reads 2026. Had the December boot refreshed the shadow to 12, the April boot would have rolled correctly.

**Fix**: in `clk_ns_read`, stamp whenever the shadow differs from the month read, not only on a roll. One compare and a branch in `.ovl`, 0 resident. The rung is already identified when this runs, so the write is to our own chip. The failure window shrinks from "a month at or after the last Set" to "off for 12 months or more", which no shadow can cover.

### sysmisc-5 - The RP5C01 is claimed or refused depending on the time of day (weakness, low, now, about +16 `.ovl`)

In 12-hour mode the chip's 10-hour digit carries the PM bit, so the probe's joined hour is 20..39 in the afternoon and 12 at midnight. Neither ever equals the BIOS's 0..23 hour (`clock.inc:1300-1311`, `:1468-1471`). The same machine is therefore claimed as rung 3 in the morning and falls to rung 5 (BIOS) in the afternoon. The Date/Time caption and the write path change with it (`ctrl.inc:4635-4642`, `clockw.inc:76`).

**Fix**: when the raw hour is out of 0..23, compare the 12-hour decode as well: `(tens & 1)*10 + units`, mapped 12→0 and then +12 for PM. No page switch and no write, so the read-only promise holds. This matters only on the Toshiba-class machines that rung exists for.

### sysmisc-6 - The Screen Saver settings window outlives the Control Panel; its Customize sibling does not (hazard, low, now, about +12 `.cold` and +15 SAVER.DRV)

- Customize: THMPAL.DRV's window is closed and released in `drv_cp_closed_x` at every panel close (`ctrl.inc:3031-3033`, `driver.inc:3087-3095`).
- Screen Saver: SAVER.DRV's window is an independent unowned window, reaped only through `ss_post_x` (`blank.inc:844-861`). A user can close the panel, leave the saver window up and press Ok. `ss_post_x` sets `[cp_wdirty]` (`:859`) with no panel open to spend it, so the save waits for the next panel close or the reboot path, and a power-off loses it.

That breaks SPEC.md 31.8's "the close is a decision the user makes" in the one place a second window can outlive the first. The two buttons on the same page also behave differently.

**Fix**: close the saver window from the panel's teardown like Customize. This needs one more `SSV_*` verb, mirrored and gated by `t_mirror`. SAVER.DRV is `DRVC_OVL` and kernel-private, so this is no package ABI. Alternative: let `ss_post_x` spend `[cp_wdirty]` when `cp_live` says no panel is open. That costs the 2 s write on Ok, which is a user decision.

### sysmisc-7 - Unloading VESA.DRV from the Drivers page redraws at a stale origin after `vid_switch` (hazard, low, now, -1 to +2 CTRL.DRV)

`cp_vid_click` says "NOTHING IS REDRAWN AFTER A SUCCESSFUL SWITCH, and that is load-bearing": the window is re-fitted and DI/BP may describe a rectangle that no longer exists (`ctrl.inc:4390-4396`).

The Drivers page's `VID_EXT` unload arm (default kern_big, `kernel.asm:858-861`) calls `vid_switch` to VGA (`ctrl.inc:7131-7138`). It then falls through `.off`/`.save`, which calls `cp_list` at `DI - CP_RX` (`:7161-7167`) and then `cp_drv_box1`/`cp_drv_line1` (`:7178-7179`), all at the pre-switch origin. The `cp_repost` that follows repaints the screen correctly. What lands first is a few draws at coordinates that belonged to the old mode: a double-draw, clipped only by `[vid_w]`/`[vid_h]`.

**Fix**: after the `vesa_row` switch, record the unload and the want/answer, then `jmp .done` past the redraw, as `cp_vid_click` does.

### sysmisc-8 - "`cp_ctl` before any side effect" is an invariant held by prose (weakness, low, record, 0 bytes)

Every static page's click proc is run up to three times per gesture (`cp_probe` 0/1/2, `ctrl.inc:440-449`). It is correct only if every action site calls `cp_ctl`/`cp_ctli` before it writes anything. All the sites read correctly today:

- theme `ctrl.inc:3138`, `:3187`, `:3234`, `:3282`; dock `:3454`, `:3469`; floppy `:3591`; scheduler `:3844`
- display `:4419`, `:4463`, `:4501`, `:4530`; date/time `:4682`, `:4732`, `:4758`, `:4771`
- sound `:5911`, `:5942`, `:5984`, `:5993`; ports `:6201`; drivers `:7058`, `:7095`, `:7124`

Two sites once shipped without the call (`ctrl.inc:2058-2061` says so).

Painter ids must also equal click ids. The prose keeps that true at `ctrl.inc:3287-3292` and `:4237-4240`; a mismatch draws the wrong control pressed. No gate checks either property. R2 removes both invariants. Short of R2, an `os88ovlchk`-style flow check (first DS store in each `CPITEM` click proc is dominated by a `cp_ctl*` call) would turn the prose into a gate.

### sysmisc-9 - On kern_big a saver wake is a full `wm_paint_all` (perf, low, now, about +22 resident `.text`, ESTIMATED)

`blk_relit` uses the damage pass only under `KHP_RELIT` (`blank.inc:530-563`), which only kern_mega defines (`kernel.asm:281-292`). kern_mega's machines are 286s; the shipped XT build pays the overdraw. SPEC.md 11.91.7, quoted at `blank.inc:536-541`, says each pixel under the top window is written 7-8 times in deskbench's scene, and `ss_stop_x` costs about 0.4 s on the 5150 (`blank.inc:723-728`).

That wall-clock cost lands on the user's first look at the screen after a wake, and on every fullscreen exit (`fsx.inc:500`). Recommend pricing `KHP_RELIT` on kern_big: about 22 `.text` bytes against a measured `flicker`/`pace` reading of the wake. Under "bytes are forever" it should be asked, not assumed. It has not been measured here.

### sysmisc-10 - Dead boot stores rest on a false "`.bss` is not zeroed" (consistency, low, now, -15 `.ovl`)

`clk_probe` stores `CLK_T_NONE`, 0 and 0 into `[clk_tier]`, `[clk_cent]` and `[clk_ref]` "because .bss is not zeroed" (`clock.inc:303-305`, `:384-388`). Kernel `.bss` arrives zeroed (SPEC.md 10/18051, `kernel.asm:5896`), `clk_probe` runs once, from `clk_init`, and `clk_ref` lives in `clk_str`, which nothing writes before the probe (`clock.inc:2272-2286`). The three stores are dead: 15 bytes of the boot overlay, nothing resident.

The same false premise appears at `clockw.inc:70-72`, `ctrl.inc:367-368` and `ctrl.inc:5597-5599`. It argues for initialised `.text` that costs the same image bytes either way, as `blank.inc:121-131` correctly says. A module image's own data is a different question; the comments do not distinguish the two.

### sysmisc-11 - The saver's idle period cannot exceed 59 minutes (size, low, record, 0)

`[ss_idle]` is a 16-bit tick count compared against the 16-bit `[ticks]` (`blank.inc:316-320`), so `SS_MAXMIN` is 59 (`:610`, mirrored at `svcfg.inc:741`). Windows 3.1 offered 1-99 minutes. Nothing here needs more, but R1's 32-bit count lifts the ceiling for free.

### sysmisc-12 - `W_FLAGS` "visible" is the literal 2 (consistency, low, now, 0 bytes)

`test byte [bx+W_FLAGS], 2` appears at `ctrl.inc:1240` and `:4813`, and in `files.inc`, `menu.inc`, `ui.inc` and `stikmenu.inc`. `wm.inc` names every other bit (`wm.inc:192-240`) but not this one. A `WF_VIS equ 2` costs nothing and makes the eleven sites greppable.

### sysmisc-13 - A selected list row's name blinks into its bar (weakness, low, record)

`cp_listrow` fills the 12 px bar and then letters the name over it (`ctrl.inc:2454-2510`). On the row gaining the selection, the name disappears into solid black for the fill-to-`font_run` gap of one fill plus about nine glyph cells. This is PERFORMANCE.md rule 2 at row scale. SPEC.md 31.1.4's "no interval in which the pane is blank" is true of the pane, not of the row.

The cheap answers both cost more calls than they save: fill only the bar's margins (three fills against one), or a taller `font_run` cell. Record it. The right home is a `font_run` variant with a vertical pad, if one is ever wanted elsewhere.

### sysmisc-14 (R1) - One clock: derive the time of day from 0040:006C (rethink, medium, now, -10 to +10 resident, ESTIMATED)

Today the kernel integrates its own clock: `clk_last`, `clk_acc`, a per-second carry loop (`clock.inc:1674-1805`) and a hibernation catch-up that walks `[clk_last]` back 32,768 ticks a pass (`hiber.inc:4626-4676`). DOS, the kernel's own kerndos (`dos.asm:14098`) and the BIOS all use the BIOS count at 40:6C, which `sch_isr` keeps by chaining the ROM every tick (`hiber.inc:4620-4624`) and which `mega.inc:1362-1369` maintains in protected mode.

**Proposal**: keep the date and a "last 40:6C seen" word pair, and derive h:m:s from the count when formatting. Formatting happens once a minute, or once a second with seconds shown, which is cheap next to the glyphs it feeds. Midnight is detected as "count < last". The Date/Time page writes the count through `int 1Ah AH=01h`.

It removes:
- sysmisc-1's drift, by construction
- sysmisc-3, since DOS and the menu bar read one number
- sysmisc-11's 59-minute ceiling: `blk_pass` can compare 32-bit
- `clk_acc`/`clk_last`, and the catch-up loop in `hiber.inc`

It costs a three-divide breakdown (kerndos's own shape, about 45 bytes) shared by `clk_fmt` and `osapi_clk_get_x`, against roughly 55 bytes of accumulator and sec/min/hour carry removed.

ABI: none. `OSAPI_CLK_GET`'s outputs are unchanged. The kern_dos handoff record's `KDL_CLK` bytes (`hiber.inc:3121-3128`) are kernel-internal.

Prior art: DOS (time of day = the BIOS count), Mac System (one 32-bit seconds counter at a fixed low-memory global, broken down by `Secs2Date` on demand), OS/2 1.x (time fields in the global info segment, read without a call).

### sysmisc-15 (R2) - Declarative Control Panel pages (rethink, medium, now for static pages, flagday for the driver-page ABI; CTRL.DRV -1,500 to -3,000, 0 resident, ESTIMATED)

`ctrl.inc` is 7,870 lines. Most of the static pages are about 150 hand-placed layout equates (`ctrl.inc:115-235`, `577-690`, `786-901`), a paint proc and a click ladder per page, a separate painter for each page's controls (`:1598-1631`), and three invariants held by prose: probe-before-act (sysmisc-8), painter id = click id (`:3287-3292`), and one greying predicate per control (`:3082-3097`).

Every page is the same five control species: radio group, check box, button, drop-down, labelled value. A table per page, e.g. `{species, rect, id, getter, setter, predicate}`, interpreted by one engine of roughly 500-700 bytes, would do the following:
- paint, hit-test, press/drag/release, grey and refuse from one description
- delete the probe trick and the id bookkeeping
- make SPEC.md 47 rule 2 (one predicate) structural

The Display, Sound and Drivers pages have bespoke parts (slot compaction, the Covox ports, the memory column). They would keep a callback hook.

ABI: none for static pages, since they are module-internal. Moving driver pages (`DSV_CPPAINT`/`DSV_CPCLICK`, SPEC.md 31.9) to the same tables would be a flag day with every page-publishing driver. List it on the break list only if the static-page engine proves itself.

Prior art: the Mac dialog item list (DITL) resources with the Dialog Manager; Windows `DLGTEMPLATE` plus `IsDlgButtonChecked`/`CheckRadioButton`; PC/GEOS's declarative UI compiled into generic objects. This is the closest peer on an 8088, and it did UI this way rather than as hand-placed code.

### sysmisc-16 - A setting change tells packages only by repainting the world (rethink, low, record, about +15 resident)

`cp_repost` sets `[cp_dirty]`, and the UI task does `wm_paint_all` (`ctrl.inc:2866-2869`, `ui.inc:962`). A package that derives state from a setting has no notice except the repaint. Examples: the 12/24-hour bit of `OSAPI_CLK_GET` (SPEC.md 37.96), theme pens, the dock edge.

Windows 3.x broadcast `WM_WININICHANGE`/`WM_SYSCOLORCHANGE` (`SystemParametersInfo`'s `SPIF_SENDWININICHANGE`). An `EVT_SETTING` record posted by `cp_repost` would be additive, with no break, about 15 resident bytes. Record it until a package actually caches a setting; the repaint covers every package that paints from live state.

### sysmisc-17 - Core settings in battery-backed CMOS (prior-art, low, record, not recommended)

The Mac kept its core settings in 20 bytes of PRAM, written immediately and surviving any disk. On AT-class machines CMOS has bytes outside the BIOS checksum range, often 34h-3Fh. The settings could survive a missing or write-protected system disk. **Not recommended**: the free bytes are BIOS-vendor-specific, a wrong guess corrupts another vendor's setup data, and the floppy-only XT, the target, has no CMOS. Written down so the next person does not have to re-derive it.

## Rethinks

R1 (sysmisc-14) and R2 (sysmisc-15) above are the two worth the owner's time:

- **R1** is a model change with no ABI cost. It fixes a correctness defect and a cross-component inconsistency, and it lifts an arithmetic ceiling. Resident cost is roughly neutral.
- **R2** is the largest module shrink available in this area, and it retires three prose-held invariants. It is module bytes, not resident ones, which matters most on the 128 KB machine where an 11 KB claim is a fifth of the heap.

Also considered and not recommended:
- Moving the panel's transient resident bytes (`cp_probe`, `cp_ctlid`, `cp_down`, `cp_only`, `cp_skip`, `cp_tfull`, `cp_lfull`, `cp_dmpar`: 8 bytes) into the image. Every access would need a `cs:` prefix, and `CSDO` on kern_mega. That is about 25 module bytes to save 8 resident, and the image's data is not writable on kern_mega without the alias dance.
- Folding the blanker into SAVER.DRV. The blanker is the fallback for every SAVER failure, including "no system disk", so it must stay resident.

## Prior art

| subsystem | DOS | Windows 3.0/3.1 | OS/2 1.x | Mac System 5-7 | PC/GEOS | AmigaOS | os8088 |
|---|---|---|---|---|---|---|---|
| clock model | time of day = BIOS count at 40:6C; date = a day counter; RTC through a device driver (CLOCK$, or ASTCLOCK for XT cards) | DOS's | kernel-maintained fields in the global info segment, read with no call | one 32-bit seconds counter in low memory, broken down on demand | its own | timer.device system time seeded from battclock.resource | broken-down fields integrated per second from `[ticks]`; RTC read once at boot through a four-rung ladder |
| settings store | CONFIG.SYS/AUTOEXEC.BAT text, read at boot | WIN.INI/SYSTEM.INI; `SystemParametersInfo` with explicit persist (`SPIF_UPDATEINIFILE`) and broadcast (`SPIF_SENDWININICHANGE`) flags | OS2.INI profile API | PRAM (20 bytes, immediate) plus System file resources | .INI | Preferences editors with an explicit **Use** (session) vs **Save** (persist) | one SYSTEM.CFG, effect immediate, record deferred to the panel close |
| panel structure | none | CONTROL.EXE hosting .CPL applets (`CPlApplet`: `CPL_INIT`/`GETCOUNT`/`INQUIRE`/`DBLCLK`/`STOP`/`EXIT`), each its own templated dialog | Control Panel PM app | Control Panel DA hosting cdevs (`initDev`, `hitDev`, `closeDev`, `nulDev`, `updateDev`, `macDev` - "do I apply to this machine?") | Preferences with modules loaded on demand | one editor per category | one window, static pages plus driver-published pages (DSV_CP*), the whole thing an on-demand module |
| screen saver | none (TSRs) | USER's idle timer sends `WM_SYSCOMMAND/SC_SCREENSAVE` (an app may veto), then runs a .SCR that draws; wake repaints | none in 1.x | third-party (After Dark: a cdev plus module files) | saver libraries chosen in Preferences | commodities | signal-gated blank as the always-available answer; SAVER.DRV modes when it can; wake input consumed; no veto, only `WF_QUIET`'s "blank instead" |

What they did better, systemically:
- **DOS's one clock.** Every consumer reads the BIOS count, so nothing can disagree and nothing drifts from it (R1).
- **Windows' templated applet dialogs and the Mac's DITL.** UI described as data and run by one engine (R2).
- **The cdev `macDev` and `CPL_INIT` refusals.** "Am I relevant on this machine" is asked of every applet through one protocol. os8088 has it only for Display (`cp_hide`, set once by `vid_probe_avail`) and implicitly for driver pages.
- **Amiga's Use/Save split** makes the deferred-persist model visible to the user. os8088 does the same thing silently, with a toast at the close.

What os8088 does better:
- The **signal-gated blank**: instant, exact and drawing-path-free, where the Windows savers repaint.
- **Wake-input consumption** treated as a safety rule.
- The **RTC ladder** finds XT clock cards with no per-card driver.
- **Driver-owned pages** appear and disappear with the driver, with no registration step.
- The **deferred single write** fits a machine where a floppy write is seconds.

## Doc/code mismatches

Each row: file:line, a short exact quote, what the code does, and proposed text. Dated documents (`docs/reports`, `docs/history`, `docs/plans/completed`) are excluded.

1. `kernel/clock.inc:20-27` "and [clk_dirty] itself went with the move." The byte is still `.bss` at `clock.inc:2180`. It is set by `clk_fld_adj` (`ctrl.inc:5257`) and spent by `cp_flush_close_x` (`ctrl.inc:5487-5493`). Proposed: "ui.inc no longer drains [clk_dirty]: the byte stays resident, set by clk_fld_adj and spent by cp_flush_close_x at the panel's close."
2. `kernel/clock.inc:153-157` "clk_mlen carries the eleven non-February answers as a 16-bit mask instead (0x15AA". The code is the parity form (`clock.inc:1807-1837`, "kernel size pass 17"). Proposed: "...clk_mlen derives the eleven non-February answers from the month's parity about August (kernel size pass 17); February is the branch."
3. `kernel/clock.inc:2193-2194` "nowhere to persist them (the kernel never writes the data disk)". `clk_h12` and `clk_secs` are persisted as SYSTEM.CFG keys 'CH'/'CS' (`driver.inc:906-909`, `:951-953`). Proposed: "--- display settings (SPEC.md 37): defaults in clk_init, persisted in SYSTEM.CFG's 'CH'/'CS' records (SPEC.md 51.5)".
4. `kernel/clock.inc:384-388` "which it needs because .bss is not zeroed". Kernel `.bss` arrives zeroed (SPEC.md 10, `kernel.asm:5896`). Proposed: "`.none` never arrives here; the CLK_T_NONE store at the top is redundant with .bss's boot zero (sysmisc-10)" - or delete the stores.
5. `kernel/clock.inc:631-633` "see clk_rp_dgno above ... clk_at_probe then tries the other mode once". `clk_rp_dgno` is retired (`:1276-1280`), and the other-mode retry is in `clk_at_read` (`:578-585`). Proposed: "the refusal trampoline ... Cold by construction - a field out of range means the byte did not decode in the mode Register B claims, and clk_at_read then tries the other mode once." The same retired reference is at `clock.inc:1523`.
6. `kernel/clock.inc:1865-1871` "out: SI = clk_str, 18..24 glyphs + NUL ... clobbers: nothing else". The code also returns CX = the length (`:1947-1952`, SPEC.md 37's table). Proposed: "out: SI = clk_str (18..24 glyphs + NUL), CX = its length; clobbers: nothing else".
7. `kernel/clock.inc:2335-2337` "a worker accepts a torn read across a rollover". Every writer mutates a whole second or a whole edit under `cli` (`:1743-1805`, `ctrl.inc:5172`), and this reads under `cli`. A worker can see a stale clock but never a torn one. Proposed: "from any task the read is a consistent instant - every writer carries a whole second under cli - though a worker may see it a catch-up behind." The same sentence is in SPEC.md 37.96 ("must accept a torn read").
8. `kernel/clockw.inc:55-63` "in: nothing; NO gfx lock held (SPEC.md 13/31.1) ... ui_task step 3 is a perfectly good place to drain [clk_dirty] from." The only caller, `cp_flush_close_x`, runs with the lock held (`clockw.inc:42-48`, SPEC.md 37). Proposed: "in: nothing; the gfx lock HELD - cp_flush_close_x is the only caller (SPEC.md 37.94). A machine without an RTC returns immediately."
9. `kernel/clockw.inc:70-72` "and .bss is not zeroed". As item 4. Proposed: "belt as well as braces: [clk_rtc] already says a rung claimed".
10. `kernel/clockw.inc:340-341` "The read half, clk_rp_get, is resident (the probe needs it) and far-called". It is assembled twice, `clk_rp_get` in `.ovl` and `clkw_rp_get` in this image (`clock.inc:1237`, `clockw.inc:645`). Proposed: "The read half is CLK_RP_GET_BODY, assembled once into the boot overlay and once here as clkw_rp_get."
11. `kernel/blank.inc:136-145` "(SPEC.md 79.4), and they are in EVERY BUILD ... it reads and writes these three bytes all the same". The block is inside `%ifdef KERN_BIG` (`:150-161`), and the comment right after it says so. Proposed: delete the paragraph from "and they are in EVERY BUILD" through "on the build that can.", and keep the pass-13 note as the statement.
12. `kernel/blank.inc:603-606` "its three callers - SYSTEM.CFG's reader (through wm_cbd), ss_set_x and the blanker's wake". `ss_set_x` is folded into `ssf_set` (`blank.inc:902`). The callers are `wm_cbd` (`driver.inc:5001`), `ss_post_x` (`blank.inc:858`) and `ssf_set`'s blanker wake (`:908`). Proposed: "its three callers - SYSTEM.CFG's reader (through wm_cbd), ss_post_x and ssf_set's blanker wake".
13. `kernel/ctrl.inc:4-45` (file header). It says "Item 0 is 'Scheduler' ... one 8-byte record", "no KD_INIT, no pool, no .bss", "with one item is indistinguishable from resetting", "make no BIOS calls", and "a new selection redraws the two panes (each erases its own rectangle first)". The code has `cp_kinit` (`:323`), `cp_sbuf` in `.bss` (`:6974`), eight static items (`:7341-7362`), and driver loads and overlay loads under the lock (`cdl_load`, `:7155`; `ss_open`, `:3143`; `cp_thmv_open`'s `drv_load_at`, `:3027`). A selection redraws two rows (`:1350-1377`). Proposed: rewrite the banner as "two-pane browser; static pages from cp_items (6- or 8-byte records) plus driver-published pages; KD_INIT cp_kinit installs the release/drag edges; resident state is a few initialised .text bytes and cp_sbuf's .bss; page clicks may do disk I/O under the held lock (driver and overlay loads, SPEC.md 18.4); a selection move redraws two list rows and the page".
14. `kernel/ctrl.inc:157-160` "fits inside CP_CH (121) with room to spare". CP_CH is 132 (`:51`). Proposed: "fits inside CP_CH (132)".
15. `kernel/ctrl.inc:198-200` "; 78: anim row 0 top / ; 79: ...bottom / ; 95: anim row 1 bottom". The real values are 94/111/127 on kern_big and 76/93/109 on kern_small (`CPH_A0Y` 98/80, `:183`, `:187`). Proposed: "; 94 (kern_small 76): anim row 0 top", "; 111 (93): ...bottom", "; 127 (109): anim row 1 bottom".
16. `kernel/ctrl.inc:354` and `:367-368` "nothing zeroes .bss (-f bin)". Kernel `.bss` is zeroed at boot (SPEC.md 10). Proposed: ".text with an initialiser, like the rest here: the value is the boot answer, written as a decision".
17. `kernel/ctrl.inc:785-787` "The click loads or unloads ON THE SPOT and then writes SYSTEM.CFG". The click only marks `[cp_wdirty]` (`ctrl.inc:7160`, SPEC.md 31.8). Proposed: "The click loads or unloads ON THE SPOT and marks SYSTEM.CFG owed; the panel's close writes it (SPEC.md 31.8)".
18. `kernel/ctrl.inc:850-857` (the paragraph "SPEC.md 31.5.1: is this draw a FULL one ... A byte rather than a register because cp_time_fld's AL and AH ..."). It describes `cp_tfull` but sits above `cp_lfull`, after the `cp_tsel` declaration; `cp_tfull` is declared at `:871`. Proposed: move the paragraph to `cp_tfull`'s declaration.
19. `kernel/ctrl.inc:2717-2721` "a 12x12 glyph is 82.8 ms of the field machine's time for the Sound page's three". The measured cost is 4.3-6.4 ms a glyph (`:1554-1559`, `:6639-6645`); 82.8 ms was the deleted clipped fallback. Proposed: "a 12x12 glyph is a measured 4.3-6.4 ms of the field machine's time (docs/reports/GLYPH-AND-LINE-COST-2026-09-10.md)".
20. `kernel/ctrl.inc:4590-4592` "writing the hardware RTC is ui_task's job, off the [clk_dirty] flag (SPEC.md 13)." It is `cp_flush_close_x`'s job (`:5486-5492`, SPEC.md 37.94). Proposed: "...and nothing here calls BIOS: the hardware RTC is written by cp_flush_close_x at the panel's close, off [clk_dirty] (SPEC.md 37.94)."
21. `kernel/ctrl.inc:4662-4665` "so the hardware RTC is written by ui_task, outside this held lock, never from here." Same fact as item 20. Proposed: "so the hardware RTC is written at the panel's close (cp_flush_close_x, SPEC.md 37.94), never from a click."
22. `kernel/ctrl.inc:5177-5180` "posts [clk_dirty] so ui_task writes the hardware RTC outside the lock", and `:5257` "ui_task step 3 writes the RTC (SPEC.md 13)". Proposed: "posts [clk_dirty], which cp_flush_close_x spends at the panel's close (SPEC.md 37.94)" and, at `:5257`, "; the panel's close writes the RTC (SPEC.md 37.94)".
23. `kernel/ctrl.inc:5313-5316` "It is one write of one 32-byte file ... the driver list, the sound route, the clock options and the scheduler mode", and `:5309-5312` "It is a near call inside .cold". The file is `CFG_FBUF` bytes with 16 keys on kern_big (`driver.inc:1024`, `:1043`), and `cp_flush_x` is `.modc`, the settings core (`ctrl.inc:5348`). Proposed: "It is one write of one small file (CFG_FBUF bytes: every key in driver.inc's CFG_DATA) ... It is a near call inside the module's core (.modc) with no kernel thunk in front of it".
24. `kernel/ctrl.inc:5597-5599` ".text and not .bss for the reason everything else here is: -f bin zeroes nothing." Proposed: "Initialised .text like the rest of the panel's resident state (0 = the notice line is blank)".
25. `kernel/ctrl.inc:7023-7026` "A settings write that cannot reach the disk says so in this page's caption - the next time the panel is opened". It is a toast at the close (`cp_flush_x`, `:5348-5398`; `cp_drv_paint`, `:6331-6334`). Proposed: "A settings write that cannot reach the disk is said as a toast when the panel closes (SPEC.md 59.6), while the driver it loaded stays loaded".
26. `SPEC.md:68808-68836` (§31 intro) "**320×140** at (160,130)", "No onkey (the panel is mouse-only)", "task-less, no KD_INIT, no pool, no .bss", "with one item that is indistinguishable", and the code block's `320, 140 ... 0, cpf_cp_onclick`. The code is 320×151 with kern_big's `cpf_cp_onkey` (`ctrl.inc:287-296`), `cp_kinit` (`:323`) and `cp_sbuf` `.bss` (`:6974`). Proposed: "**320×151** at (160,130) ... kern_big carries an onkey (SPEC.md 31.9.2) for a driver page's typed field; KD_INIT `cp_kinit` installs the release and drag edges; resident state is initialised `.text` bytes plus `cp_sbuf`'s `.bss` scratch" and the template line `dw 160, 130, 320, 151, cp_ttl, cpf_cp_paint, cpf_cp_onkey, cpf_cp_onclick`.
27. `SPEC.md` §31.2 table, `cp_entry` "`6*AL`", `cp_onclick` "redraws both panes (`cp_list` + `cp_page`)", `cp_list` "so it doubles as the redraw path when the selection moves", `cp_sel` "Written only by `cp_onclick`", and `cp_dirty` "Set to 1 by `cp_sched_click` only". The stride is 8 on kern_big (`ctrl.inc:483-489`). A selection redraws two rows via `cp_listrow` (`:1350-1377`). `[cp_sel]` is also written by `ui.inc:523`/`:3403` (CP_ITIME), `driver.inc:6084` (CP_IDRV) and `cp_drv_gone_x` (`ctrl.inc:1279`). `[cp_dirty]` is set by `cp_repost` from five pages (`:2866-2869`). Proposed: "`CP_ISTRIDE*AL` (8 on kern_big, 6 on kern_small)"; "moves the bar with two `cp_listrow` calls and swaps the page (§31.1.4)"; "the whole-pane paint, used when every row moved"; "written by `cp_onclick`, by ui.inc's clock click (CP_ITIME), by `drv_notice` (CP_IDRV) and clamped by `cp_drv_gone_x`"; "set (with `[cp_wdirty]`) by `cp_repost` whenever a page changes the world".
28. `SPEC.md` §31.3/§31.4 "`CP_IDRV` is **2** and `CP_ISND` **3**" (twice). The code has CP_IVID 3 and CP_ISND 4 on kern_big, and CP_ISND 3 on kern_small (`ctrl.inc:510-532`). Proposed: "...`CP_IDRV` **2**, `CP_IVID` **3**, `CP_ISND` **4** on kern_big (kern_small, with no Drivers row: `CP_IVID` 2, `CP_ISND` 3)".
29. `SPEC.md` §31.8 "Each page calls `cp_flush` after it changes anything, and `cp_flush` writes `SYSTEM.CFG` (§51.5) — one 32-byte file ... and the back buffer". Pages set `[cp_wdirty]` and the close writes once (the section's own next paragraph). There is no back buffer (§32 retired). Proposed: "Each page marks `[cp_wdirty]` when it changes anything, and the panel's close writes `SYSTEM.CFG` (§51.5) — one file carrying every key in `CFG_DATA`".
30. `SPEC.md` §31.8 "`cp_flush_x` is a near call inside the cold segment". It is in `.modc` (the settings core, `ctrl.inc:5348`). Proposed: "`cp_flush_x` is a near call inside CTRL.DRV's core (`.modc`)".
31. `SPEC.md` §31.8 "It is reported in the Drivers page's caption ... **`cp_drv_cap` is why that is now true**". It is a toast (`ctrl.inc:5348-5398`; `cp_drv_paint` "NO save caption", `:6331-6334`). Proposed: "It is said as a toast at the close (§59.6); `[cp_dsave]` survives only to keep `[cp_wdirty]` set for the retry."
32. `SPEC.md` §37 "All code is near `.text`". The probe is `.ovl` (`clock.inc:217`), `osapi_clk_get_x` is `.cold` (`:2318`), and the field editor and write half are CTRL.DRV (`ctrl.inc:5106-5112`). Proposed: "The software clock and the bar's formatters are near `.text`; the RTC probe is the boot overlay's; `OSAPI_CLK_GET` is `.cold`; the field editor and the RTC write half ship in `CTRL.DRV` (§37.93, §37.94)."
33. `SPEC.md` §37 "**The two display settings** ... return to their defaults at boot ... it is deliberately not built". They are in SYSTEM.CFG 'CH'/'CS' (`driver.inc:906-909`). Proposed: "The two display settings ... are persisted in `SYSTEM.CFG` (§51.5, keys 'CH' and 'CS') with every other Control Panel setting, and default to 24-hour with no seconds."
34. `SPEC.md` §37 *Advancing* "month lengths from a 12-byte table" and *Month LENGTHS* "one 16-bit mask ... the 3-bytes-cheaper `mov ax, mask / shr ax, cl` form". The code uses parity (`clock.inc:1807-1837`), and `t_mlen.py` reads that (`tests/unit/t_mlen.py:9-13`). Proposed: "month lengths from `clk_mlen` - February a branch, the eleven others a parity about August (`cmp al, 8 / adc al, 0 / and al, 1 / xor al, 31`) - which `tests/unit/t_mlen.py` reads back out of the binary"; drop the mask paragraph's register discussion except "`clk_mlen` must not spend AH".
35. `SPEC.md` §37 `str_len` row "SI and every other register preserved". SI comes back one past the NUL (`clock.inc:1957-1961`). Proposed: "Out: CX = its length, NUL excluded; SI one past the NUL; every other register preserved."
36. `SPEC.md` §37 `clk_fld_adj` row and §37's ownership text, if read with `ctrl.inc:5177`. They are correct; only the code comments in items 20-22 are stale. No change.
37. `SPEC.md:117173` §64.1 "`BLK_IDLE` is **5462**". The code is `SS_MINS * SS_TPM` = 5 × 1,092 = **5,460** (`blank.inc:58-60`). Proposed: "...so five minutes is 5,461.95 of them; `BLK_IDLE` is `SS_MINS × SS_TPM` = **5,460**, the minute rounded down to 1,092 ticks."
38. `SPEC.md` §64.1 "Two compares per UI pass". There are three on kern_big (`[ss_post]`, `[blk_act]`, `[blk_on]`) and two on kern_small (`blank.inc:280-284`, `:307-317`). Proposed: "Three compares per UI pass on kern_big and two on kern_small".
39. `SPEC.md` §64.2 "The verdict is banked across the `int 16h` in `pushf`/`popf`". The fetch now precedes `blk_wake` and the pair is deleted (`ui.inc:178-191`). Proposed: "the key is fetched first and `blk_wake` decides after it, so there is no verdict to carry across the `int 16h` (§9.7.1)".
40. `SPEC.md` §64.5 "**No API slot.** A package cannot ask whether the screen is dark, force it dark, or suppress it." `OSAPI_WM_QUIET` (§79.10) chooses blank-instead-of-saver. Proposed: append "— §79.10's `OSAPI_WM_QUIET` is not that slot: it chooses which answer a session gets, never whether there is one."
41. `SPEC.md` §79.1 "`blk_set(1)  ->  ss_set_x  ->  ss_start_x`" and §79.6 "`ss_set_x` brackets the session". Both were folded into `ssf_set` (`blank.inc:883-907`, kernel size pass 16). §79.1.1 already says `ssf_set`. Proposed: "`blk_set(1) -> ssf_set` (its `.start` arm)", "`blk_set(0) -> ssf_set` (its `.stop` arm, `ss_stop_x`)", and "`ssf_set` brackets the session with `cursor_hide` / `cursor_show`".
42. `SPEC.md` §79.4 "**All three bytes are carried in EVERY build**, `kern_small` included". They are kern_big's alone (`blank.inc:146-161`, kernel size pass 13), and kern_small reads no SYSTEM.CFG. Proposed: "**The three bytes are kern_big's**: kern_small reads and writes no `SYSTEM.CFG` (§51.5) and has no saver page, so its `[ss_idle]` stays at `BLK_IDLE`."
43. `SPEC.md` §79.4 "Five bytes in the kernel's `.text`, four of which the overlay is handed a pointer to". The block is six bytes, `ss_idle` being a word, and the overlay's pointer reaches all of it (Test writes +4). Proposed: "Six bytes in the kernel's `.text` (five fields, `ss_idle` a word), all handed to the overlay as one block".
44. `SPEC.md` §79.7 "gains a `Screen Saver` button under its two radio groups, at pane-relative y 114". It is in the bottom-right corner at (112,110) (`ctrl.inc:212-217`), as §76.4 says. Proposed: "gains a `Screen Saver` button in the pane's bottom-right corner (pane-relative 112,110, §76.4)".
45. `SPEC.md` §79.7 "A page is drawn by the on-demand `ctrl` module (§2.8), whose data stays in the kernel's `.text`" and "`ctrl`'s strings are `.text` and resident (§2.8) where its code is not". Page strings moved into CTRL.DRV's image in kernel size pass 8 (`ctrl.inc:7301-7312`). Proposed: "...its strings were `.text` and resident when this was decided; they are CTRL.DRV's own since kernel size pass 8 (§2.8.6), so a label here would now cost the module, not the kernel - the window stays for its own reasons".
46. `SPEC.md` §79.9 "It keeps §64's blanker and honours `[ss_mins]`, including the zero that turns the feature off." kern_small has no `[ss_mins]` and no SYSTEM.CFG (`blank.inc:146-161`). Proposed: "It keeps §64's blanker at the fixed `BLK_IDLE` (five minutes); there is no setting to turn it off on that build."
47. `SPEC.md` §76.4 "their eleven labels would be 150 bytes of the kernel's `.text`, where a page's strings have to live (§2.8.1)", "What lands in the kernel is the strings and the `cp_items` row", and "six static rows is exactly nine". Strings and `cp_items` are in the image (`ctrl.inc:7255-7266`, `:7341`). kern_big has eight static rows and the list scrolls (§31.1.5, `ctrl.inc:84-94`). Proposed: "...eleven labels in a module whose strings were then resident" ; "What lands in the kernel is nothing: the strings and the `cp_items` row are CTRL.DRV's (§2.8.6)"; "eight static rows on kern_big; the list scrolls past nine (§31.1.5)".
48. `SPEC.md` §76.5 "A `'TH'` key, one byte ... The map stores `[thm_dark]` ... A byte from a future version leaves Bright standing". 'TH' is ver 3, 5 bytes, theme, zoom and the three saver bytes (`driver.inc:957-959`). The byte is `[thm_kind]`, and `thm_set` owns the refusal (`driver.inc:4974-4984`). Proposed: "A `'TH'` key, ver 3, five bytes (the theme, §11.99's zoom and §79.4's three) ... The map stores `[thm_kind]`; the unpack calls `thm_set`, which owns both refusals - an unknown kind and Color without colour (§76.12.1)."

## Open questions

1. Has the 30 s/day gain (sysmisc-1) been seen on the owner's 5150? It should show as the menu bar running a minute fast after two days up. It is predicted from arithmetic, not measured.
2. R1 or the narrow sysmisc-3 fix? The `int 1Ah AH=01h` write fixes DOS's time today in CTRL.DRV; R1 makes it structural and also absorbs sysmisc-1 and sysmisc-11.
3. Should the saver settings window close with the panel, as Customize does (sysmisc-6), or should its Ok be a save in its own right?
4. Is `KHP_RELIT`'s about 22 resident bytes worth the wake repaint on kern_big (sysmisc-9)? That needs a MartyPC `flicker`/`pace` reading of a saver wake, which this review did not take.
5. Should kerndos take its date from `OSAPI_CLK_GET` rather than `int 1Ah AH=04h`? The DOS area owns that call; it is the date half of sysmisc-3.
