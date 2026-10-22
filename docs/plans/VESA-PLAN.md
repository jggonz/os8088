# VESA — delivery plan

High-resolution, deep-colour desktops on a VBE card, through a loadable
video driver. The binding contract is SPEC.md §120; this is the story list
and the record of what each wave actually did. The diary is
[HOW-VESA-WAS-BUILT.md](../HOW-VESA-WAS-BUILT.md).

**Shape of the work, in one paragraph.** A fifth adapter kind, `VID_VESA`,
whose renderer is `VESA.DRV` (class `DRVC_VIDEO`). The kernel's planar
drawing bodies each gain one compare and an untaken branch (`VIDEXT n`,
SPEC.md §120.3) and far-call the driver when it is attached; the driver
probes VBE at attach, offers every 8/15/16bpp packed mode up to 1024x768,
picks the largest (or the user's `VE` choice), fills `vid_tab` row 4 and
draws into the 64KB window A through `4F05h` banking. The desktop stays 16
colours at any depth; one appended slot, `OSAPI_GFX_BLITD`, lets a package
draw at the real depth. QEMU's standard VGA is the fixture; MartyPC and the
86Box XTs have no VBE and are the negative control.

## Delivery stories

Each wave is a reviewable commit pushed to the same draft pull request.
A story is ticked only when its outcome has been seen on the glass or
asserted by the row it names. "Pinned" means the contract exists and the
code does not.

- [x] **S0 Foundation.** Isolated worktree (`~/Repos/os8088-vesa`,
  branch `feature/vesa`), SPEC.md §120, this plan, the diary, the CLAUDE.md
  and AGENTS.md rows, a draft PR. Outcome: the contract is reviewable before
  a byte of code.
- [x] **S1 A desktop at 1024x768.** As a user with a VBE card I tick `VESA`
  on the Drivers page (or boot a disk whose `SYSTEM.CFG` wants it) and the
  desktop comes up at the largest mode the card offers, with the menu bar,
  text, the desktop dither and the arrow drawn correctly. **Shipped:**
  `VID_VESA`, `vid_tab` row 4, `vid_depth_set`'s `[vid_ext]`, the `VIDEXT`
  hooks at `vga_solid_rect` (fill and XOR), `gfx_fill_gray`, `gfx_fill_pat`,
  `vga_save_vram`/`vga_restore_vram`, `font_char`, `ico_core`, the three
  cursor arms and `vid_setmode`; refusals at `gfx_blitp`, `gfx_spans`,
  `gfx_scroll`, `gfx_blit1`; `gfx_blit4` and `font_run` routed to their
  slow paths (every run a hooked fill). `vesa_row` (a `DRVC_OVL` row, not a
  class - the decisions table), `vx_boot_x` attaching and switching at the
  end of boot, the Drivers page switching back before an untick. Driver:
  the VBE 1.2+ probe, the 16-record mode table, Auto, `VXV_INIT` filling
  `vid_tab` row 4, the eleven bodies at 8 and 16bpp, `4F05h` banking. `make
  vesatest`, `tests/vesa.py` (soak), `NOVIDEXT=1` in the build matrix.
  **Seen:** QEMU's standard VGA comes up at 1024x768x16 (`build/vesa-s1.png`
  in the diary): menu bar with its logo, titles, date and clock; the dither;
  the arrow; the dock's strip. **Not yet:** the dock's tiles and the
  desktop's drive icons are `gfx_blit1` bands, refused until S2, so both
  areas are blank. **Measured** (`tools/kernsize.py`, kern_big): `.text`
  +409, `.cold` +215 (of which `vx_boot_x` is 116 - the blob was full to 2
  bytes), `.lowbss` +128 (the mouse ISR's stack), `.bss` +4; no rung
  crossed, `KERN_SIZE` 97,792 unchanged. Not the private driver stack the
  contract first described: the mouse ISR's stack doubled instead (SPEC.md
  §120.3.3 says why).
- [x] **S2 Every primitive.** **Shipped:** `gfx_blit4` (nibbles, both axes
  clipped by the driver), `gfx_blit1` through its own far entry
  `vx_callf1` at `.ybot` (the pen, the head mask, the tail), `gfx_scroll`
  (rows through a bounce buffer), `gfx_spans`, and `font_run`'s one-pass
  body for a run with no region armed and not disabled (either still takes
  the pair). `gfx_blitp` refuses by contract. `tests/unit/t_vidext.py`
  (fast) and `tests/vidext.txt`: every dispatch site registered as hooked
  or exempt with a reason, every selector emitted once, no refusal stub in
  the driver's table. **Seen:** the drive icons and labels, the dock's
  tile, the File menu dropped (greyed items stippled through the pair) and
  closed with the dither back whole, the Disk window with its title band,
  folder icons, buttons and scrollbar (`docs/vesa/s2-disk-window-1024x768x16.png`).
  `tests/vesa.py` drives that: a press on File, a release away, a
  double-click on A:. **Found by the gate and fixed:** a restore that put
  the pixels back at x = 0 (the sub-rect arithmetic spent AX) - the row the
  gate now checks at both ends; a frame pointer read where the caller's BP
  (the band stride) was meant; a run whose pixel count spent its x.
  **Measured** (kern_big): `.text` +508, `.cold` +226, `.lowbss` +128,
  `.bss` +4 against main; the image rung crossed once, `KERN_SIZE` 98,304
  (2,560 spare). **Not yet:** a window drag and resize on the glass
  (the drag outline is the XOR fill, which S1 drew) - S8's driving pass.
- [x] **S3 16-bit colour.** **Shipped at S1, and smaller than written:**
  the 15/16bpp path is the RGB565 table (`vx_pal16`, a 15bpp mode gets its
  own packing) and the 8bpp one loads DAC 0..15 after the mode set; Auto
  ranks by area then depth, so it takes 16 over 8 at the same size. **The
  "re-derived whenever the kernel writes the DAC" half is moot**: the kernel
  never programs a palette or a DAC entry after boot (SPEC.md §39.4,
  `vidsel.inc`'s own words) - the Bright/Dark theme and the Color presets
  are pen choices inside the sixteen, and Force Color's one DAC load runs
  before any driver exists. So the table is the EGA-compatible sixteen,
  fixed. **Seen:** white reads back white and black black at both depths
  (`tests/vesa.py`, `tests/vesa8.py`); the XOR fill at 16bpp through the
  reverse lookup (the drag outline, the menu bar's highlight). Nothing
  for a theme change to track, so no row reads pixels after one.
- [x] **S4 Choosing a mode.** **Shipped, and not as written:** the class is
  ONE BYTE on the `VM` record (ver 3), not a `VE` key - the blob had two
  bytes and the settings buffer one (SPEC.md §120.2.3 has the arithmetic);
  and there is NO Mode button - a VGA+mono machine lists three adapters,
  and the pane at four rows of 12 has no room beside Force Color - so the
  VESA row CYCLES its class when clicked while already chosen
  (`VXV_NEXT`), its label composed at paint as `Vesa WxHxB` / `Vesa auto`,
  padded to the longest so a shorter one erases the one before. Set
  Primary sends `VXV_PICK` and, if the VESA display is already up, leaves
  for the VGA and comes back. Applied at boot (`vesa8.img`'s record names
  800x600x8 and gets `0103h` over Auto's `0117h`), decaying to Auto when
  the card lacks the class (the driver's `vx_select`). **Seen:**
  `docs/vesa/s4-display-page-cycled-640x480x8.png`, then the desktop at
  640x480x8 (`s4-desktop-640x480x8.png`). **Verified by** `tests/vesacp.py`
  (soak), which found two bugs in the first cut: the label's tail left on
  the glass, and the driver's walk clobbering the painter's DI so the
  second cycle drew its label off the pane. `tests/dispcp.py` reads the
  row pitch out of the build. **Not done:** the `-vga cirrus` leg (that
  card's best is a different list; a row for it is S8's call).
- [x] **S5 The rest of the system.** **Shipped:** `fsx_capstab`'s VESA row
  (VGA's `0x01EF` - without it kind 4 indexed past the table);
  `fsx_leave` restores through `vid_setmode`'s arm; hibernate greys itself
  as `Hibernate (VESA)` while `[vid_kind]` is `VID_VESA`
  (`menu_s_hibervx`, the one resident string) and `OSAPI_DRV_SUSPEND`'s
  sweep keeps `vesa_row`; `EXTD.DRV`'s `ext_extend_b` answers CF = 1 on a
  VESA primary; `OSAPI_VIDEO` answers DL 4 / DH the depth through
  `vid_disp_planes`'s arm. Shutdown and reboot reach text mode through the
  BIOS mode set they always used. **Not driven on the glass yet:** a
  fullscreen bracket entered and left from the VESA desktop (WireBall, the
  saver) - S8's driving pass, by the plan's rule that a measurement and a
  look are taken in the final pass. The `hibernate` and `fsx*` rows on VGA
  are in S8's soak union.
- [x] **S6 A package at depth.** **Shipped:** `OSAPI_GFX_BLITD` at
  `0x04AB` - the blit form (pixels as the mode packs them, clipped to the
  display by the driver) and the palette form (`CX = 0`, DAC entries 16..255
  at 8bpp); `gfx_blitd` in the kernel, `VX_BLITD` in the driver, the ABI
  lock updated with no `PKG_FMT` bump; `VID_VESA` in the SDK, and
  `OSAPI_VIDEO`/`OSAPI_WM_DISPLAY` answering DL 4 with DH 8 or 16 (the
  window query answered 1 for kind 4 before - the first thing the gate
  found). **The consumer:** PiXEL at 16bpp builds an RGB565 table beside
  its dither tables (SPEC.md 106.27), composes a row of words through it
  and puts each up with one call; at 8bpp and everywhere else it keeps the
  sixteen. **Seen:** CITY.PCX in its own 256 colours on the 1024x768x16
  desktop (`docs/vesa/s6-pixel-city-1024x768x16.png`). **Verified by**
  `tests/vesablitd.py` (soak): the machine boots into PiXEL through an
  autostart link, and the row holds the table to the palette, the claim to
  the 31 KB layout and the glass to the palette's 565 - 216 palette colours
  on the glass. **Four bugs the row found:** the window-display query's
  depth; PiXEL's aspect and layout tables indexed by a kind of 4 (the
  picture came out one row tall); the autostart launching BEFORE the boot
  switch, so a package built its tables for the VGA (`vx_boot_x` moved
  ahead of `as_boot` in `kmain_o`); and a table lookup whose index kept the
  previous shift's carry (blues the palette did not hold). **Not done:**
  the 8bpp palette form has no consumer - PiXEL's 256-entry master against
  240 free entries is a squeeze it does not carry - and no 15bpp card has
  been seen (the contract says what a 565 block does on one).
- [x] **S7 Where the time goes.** **Shipped:** `VESABENCH`
  (`tests/vesabench/`, `make vesabench`), a benchlib package that runs and
  saves itself on its first paint, opened at boot by an autostart link, so
  86Box needs nobody to click; `vm/486-vesa`, the 486DX2/66 with a Cirrus
  CL-GD5428 (VBE 1.2) in place of the OTI-067, which the driver brings up
  at 1024x768x16, and `vm/486-vesa-desk` (`make 486-vesa-desk`), the same
  machine booting `vesa.img` and the apps disk for a human to look at the
  desktop; `tests/vesabench.py`, its QEMU run (soak: behaviour, not
  time). **Found on that desktop machine, after S8:** the pattern body
  (`vxp_pat`) copied the tile with `rep movsw` between the clip and the
  width, so CX - the rect's right edge - was 0 when the width was cut
  from it, and a Disk window's three chevron rows (the only pattern fill on
  an idle desktop) ran 65,433 pixels each across the screen as an 8-pixel
  stripe band. One `push cx`/`pop cx`; `tests/vesa.py` now opens B:\\APPS
  and reads the desktop's left margin as a CHECKERBOARD (its first cut
  asked only for black and white in a block, which the stripes satisfy),
  red on the driver as it was and green on the fix. **And from a sweep of
  every package on the apps disk** (one boot each, VESA beside VGA): the
  4-bit blit loaded the stride over x (WireBall's art a window of palette
  noise; `tests/vesablit4.py`, pixel for pixel); `wm_pref_take` folded only
  EGA, so kind 4 read past `OS88_PREFER`'s table and nine packages opened at
  the sizes it found there (`tests/vesapref.py`, Solitaire's width from its
  own source); `OSAPI_FSX_CAPS` answered kind 4, which no package reads as a
  mode it knows - Gorillas drew planar onto the packed desktop - so it
  answers `VID_VGA` and `OSAPI_FSX_SURF` refuses a same-mode bracket on the
  packed surface (`tests/vesafsx.py`, Gorillas into mode 12h and back); and
  the disk widget sat past the bar composer's 62 cells on a 1024-wide bar,
  so `fpg_finish` erases its band rows itself (`tests/vesa.py`). **And
  from playing Gorillas past its setup**, which the sweep cannot: seven
  sites read "colour" as DH = 4 where the packed display answers 8 or 16
  (SPEC.md 120.5's package rule for DH) - Gorillas' tables, `os88ui.inc`'s
  `UI_ISMONO` and its two save-under sizers, ArtfulType's sizer, Frotz,
  Solitaire, Weave's canvas and `os88.h`'s advice; the sizers claimed a
  sixteenth of the save. Colour is DH >= 4 and a save is DH x rows x
  columns, DX:AX kept (`tests/vesagor.py`, `tests/vesasu.py`,
  `tests/vesapref.py`'s card back). **Measured** (`docs/reports/VESA-BENCH-486-2026-10-22.md`): the
  BIOS call is 7.0 us and a 624x200 fill 23.3 ms across seven windows -
  **0.2%** - so **the direct window register is not taken**; the fill is
  one `rep stosw` to the edge at 10.7 MB/s, the bus. **What it changed:**
  the depth blit and the restore copied a word and tested the edge a pixel
  at half that rate; `vx_rowcopy` is one `rep movsw` to the edge, and the
  depth blit went 7.9 → 3.4 ms. `OSAPI_GFX_BLIT4` of the same rows is
  5.3x the depth blit; a 78-cell text row is 5.5 ms. PERFORMANCE.md Part 5
  carries the four rows. **Not done:** a window repaint as such is the
  report page's row (275 ms for 22 text rows, method T) and not a desktop
  window's; a real card is a field run.
- [x] **S8 Acceptance.** `NOVIDEXT=1` is in the build matrix
  (`t_buildmatrix`'s `novidext`) and measured against main: `.text` +16,
  `.cold` +101, so the hook itself is `.text` 546 / `.cold` 146 (SPEC.md
  §120.1, docs/KERNEL-MEMORY.md's history row); docs/TESTING.md carries
  the recipe; docs/INDEX.md regenerated (checked by every `make`). **The
  deduplicated soak union ran once** (2026-10-22, `build/soak-union.log`):
  vesa, vesa8, vesacp, vesablitd, vesabench, dispcalcx, dispcorner,
  dispfsx, dispsaver, disptext, dispsave, dispprefer, hibernate,
  hibernatedrv, fsxclip, fsxdisp, wireball, wireballvideo, autostart,
  pxopen, pxopenpng, pxopencga, pxpaint, pxdraw, pxdrawvga, pxfolder, pxfsx,
  pxparts, cpnames - **29 passed, 0 failed** in 1,078 s; then `make
  test-full` once: 61 passed, 1 skipped (`ctoolchain`, no C compiler
  here), and the two host-side rows it failed - a stale docs/INDEX.md after
  §106.27 and the project guide's lock on the Control Panel page - paid
  with the regenerated index, the Display page's words for the Vesa row,
  and a page for `VESA.DRV` (`tools/guide/manuals/VESA_DRV.md`); both
  rows re-run green. **Hardware-only limitations,
  stated:** no real VBE card has been booted - QEMU's Bochs VBE and 86Box's
  Cirrus 5428 are the two fixtures, and a real card's `4F05h` latency, a
  window granularity that is not a power of two, a mode list inside the
  info buffer and a 15bpp packing are each untested by design or by
  absence; no 8088 can reach any of this (§120.1). **Not driven on the
  glass:** a fullscreen bracket entered and left from the VESA desktop
  (`fsx_capstab`'s row is VGA's and `fsx_leave` restores through the
  driver's `vid_setmode`, and the `fsx*` and wireball rows pass on the
  VGA), and a window drag and resize at depth beyond what `vesa.py`'s menu
  and Disk window exercise. Both are a field run's first look.

## Decisions already taken, and the alternatives judged

| decision | alternative | why this one |
|---|---|---|
| hooks in `kern_big`, ~one image rung, with `NOVIDEXT=1` as the A/B | a fourth kernel, `kern_vesa` (kern_emu's shape), costing the XT nothing | a VESA user boots the standard disk and the card is found; a fourth kernel is a fourth system disk, a fourth live image and a fourth row in every gate that walks "every shipped image". The owner may still prefer it — the hook is written so that `VID_EXT` can be moved under a build define in one line |
| the desktop stays 16 colours | a 256-colour or RGB desktop | `[gfx_color]` is a nibble in every kernel body and every package; widening it is a rewrite of §5, §6, §25 and the SDK. Depth reaches a package through one slot instead |
| banked window A, 8086 code | the linear framebuffer through unreal mode | unreal mode is 386 code that any segment load undoes, and this kernel reloads segments on every switch (SPEC.md §120.2.3) |
| the driver owns the cursor whole | hooking `vga_save_vram`/`vga_restore_vram` under `cur_*` | a 16bpp cell is 512 bytes against `cur_save`'s 192: the driver's own buffer is where it fits. Its window switch is an `int 10h`, so the mouse ISR's private stack is 256 bytes under `VID_EXT`; a private stack in the driver was refused - two tasks preempted inside one `int 10h` would share it |
| `vx_boot_x` in `.cold` | the blob, beside `vmm_boot_x` | the blob measured 6,142 of 6,144 bytes; 116 resident bytes inside the cold rung's slack until the blob sheds a sector's worth (S8 names it) |
| `DRVC_OVL` with a row, no class | a `DRVC_VIDEO` class with a `DSV_*` table | the kernel is the only caller and holds the far pointer; a class costs a publication slot, a copied table and three per-class arrays for nothing to find |
| `[vid_planes]` = the bpp | a separate depth byte | the three save-under sizers already multiply by `[vid_planes_w]`; at 8 and 16 the formula gives the packed byte count unchanged |
| `gfx_blitp` refused on VESA | translating plane-major bytes to packed | the source is a VGA plane layout; Paint already carries the `gfx_blit4` fallback the refusal routes to |
| hibernate greyed while VESA is primary | switching to VGA inside HIBER.DRV's sweep | `vid_switch` is CTRL.DRV's and the blob's; a third expansion is a decision with bytes attached. S5 took the greying: one string, one compare |
| the class is one byte on the `VM` record | a `VE` key of its own (4 bytes) | the boot blob had 2 bytes left and the settings buffer (`ovc_buf`, blob bytes) had 1; `VM` goes to ver 3, the §39.11.3 story once more |
| the VESA row cycles its class when clicked while chosen | a Mode button beside Force Color | four rows at a 12-pixel pitch already spend the pane; a button is 40 pixels the pane does not have on a VGA+mono machine |
| the `VXV_*` verbs keep every register but their outputs | the page saving its own DI/BP around the call | SPEC.md §1's register discipline, and the symptom (a label drawn off the pane on the second cycle only) is the kind that stays hidden |
| PiXEL at depth only at 16bpp; the 8bpp desktop keeps the sixteen | loading the master's palette into DAC 16..255 | a 256-entry master against 240 free entries is a nearest-colour squeeze PiXEL does not have; the palette form ships for a package that has fewer colours than that |
| `OSAPI_GFX_BLITD` clips to the display and not to §11.3's region | routing it through `gfx_blit4`'s clip walk | the walk is the kernel's and is entered by the kernel's own body; a package draws inside a content rect it knows to be in front, which is what PiXEL's canvas is |
| one row a call at depth | a band of sixteen rows | the band buffer is 5,888 bytes and a 16bpp row of a 1024-wide canvas is 2,048; the far call is ~50 us on the 386 this is for |
| no direct window register | the Bochs `dispi` index 5 in place of `4F05h` on QEMU | measured on 86Box's 486: the BIOS call is 0.2% of a window-sized fill, and the bus is the rest (docs/reports/VESA-BENCH-486-2026-10-22.md) |
| Extend refused while VESA is primary | a two-depth straddle | §39.14.7's cut assumes both displays share a renderer; it does not, and no machine here has the pair |

## Existing facilities reused

Read first: docs/INDEX.md, SPEC.md §39 (adapters, `vid_tab`, `vid_switch`),
§5 (the drawing bodies and their raw entries), §51 (the driver contract,
`DSV_*`, `drv_tab`, `SYSTEM.CFG` keys), §9.11 (VMMOUSE.DRV, the driver that
exists for the same reason), §53 (fullscreen), §57.4 (`vid_dbg_blk`, the
pointer-block idiom `vx_blk` copies). Reused as-is: `GFXCLIP`/`GFXDISP`
(the hook sits below both), `DRV_ATTACH`/`drv_publish`, the `VM` key's
apply-at-boot path, the Display page's compacted radio rows, `ethertest`'s
configured-disk recipe, `tools/shot.py --crop --zoom`, `tools/mouse.py
--screen`.

## Verification discipline

Per wave: `make` (the fast tier), `make KERN_SMALL=1`, `tools/kernsize.py`
on both kernels, one QEMU boot to look at (screenshots cropped and zoomed
into the diary), and the rows named above. **No soak row and no `test-full`
until S8** — CLAUDE.md's rule: the deduplicated union runs once after the
last batch, and a failing final pass re-runs only what failed.

What no emulator here can show: a real VBE card's `4F05h` latency, a BIOS
whose window granularity is not a power of two, and a card whose mode list
lives inside the info buffer. The first is 86Box's to approximate; the other
two are refusals the driver makes by design and a field report would be the
first evidence either way.
