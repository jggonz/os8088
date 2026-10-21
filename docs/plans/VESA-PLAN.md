# VESA — delivery plan

High-resolution, deep-colour desktops on a VBE card, through a loadable
video driver. The binding contract is SPEC.md §119; this is the story list
and the record of what each wave actually did. The diary is
[HOW-VESA-WAS-BUILT.md](../HOW-VESA-WAS-BUILT.md).

**Shape of the work, in one paragraph.** A fifth adapter kind, `VID_VESA`,
whose renderer is `VESA.DRV` (class `DRVC_VIDEO`). The kernel's planar
drawing bodies each gain one compare and an untaken branch (`VIDEXT n`,
SPEC.md §119.3) and far-call the driver when it is attached; the driver
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
  branch `feature/vesa`), SPEC.md §119, this plan, the diary, the CLAUDE.md
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
  §119.3.3 says why).
- [ ] **S2 Every primitive.** `gfx_fill_gray`, `gfx_fill_pat`, `gfx_xor_fill`
  (menu highlights), `vga_xor_rect_vram` (drag outline), `gfx_spans`,
  `gfx_blit4`, `gfx_blit1` with the pen, `gfx_save`/`gfx_restore`,
  `gfx_scroll`, and `gfx_blitp`'s refusal. Outcome: Paint, Note Pad, the
  Disk window, Calculator, Solitaire and the Control Panel all draw as they
  do on VGA; menus open and close without a ghost; a window drags and
  resizes. `tests/unit/t_vidext.py` keeps the selector inventory honest.
  Verified by: `tests/vesa.py`'s driving half (a menu, a window, a drag).
- [ ] **S3 16-bit colour.** 15/16bpp modes rendered through an RGB-word
  path; the 16-entry table re-derived whenever the kernel writes the DAC
  (Bright/Dark theme, the Color presets of §76.14); Auto takes the deepest
  mode. Outcome: the Dark theme and a Color preset look the same at
  1024x768x16 as at 640x480x4. Verified by: `tests/vesa.py` reading pixels
  back after a theme change; `tests/vesa8.py` for the 8bpp palette.
- [ ] **S4 Choosing a mode.** The Display page's `Vesa WxHxD` row named from
  the driver's record; the Mode cycling button; the `VE` key written,
  applied at boot and at Set Primary, and decaying to Auto when the card
  lacks the mode. Outcome: a user picks 800x600x8, reboots, and gets it; the
  same disk in a QEMU `-vga cirrus` machine gets that card's best. Verified
  by: `tests/vesacp.py`.
- [ ] **S5 The rest of the system.** Fullscreen enter/leave (WireBall, the
  screen saver, PIXELSTEIN) comes back to the VESA desktop; shutdown and
  reboot reach text mode; hibernate greys itself with the reason while VESA
  is primary and `OSAPI_DRV_SUSPEND` skips the class; Extend is refused
  while VESA is primary; `OSAPI_VIDEO` answers DL 4 / DH 8 or 16 and
  `OSAPI_WM_PREFER` takes VGA's row. Verified by: `tests/vesafsx.py`, and
  the `hibernate` and `fsx*` rows unchanged on VGA.
- [ ] **S6 A package at depth.** `OSAPI_GFX_BLITD` (and its palette form at
  8bpp), the ABI lock updated without a `PKG_FMT` bump, and one consumer:
  PiXEL shows a GIF or BMP in true colour on a 16bpp desktop and falls back
  to its 16-colour path everywhere else. Verified by: `tests/vesablitd.py`
  and PiXEL's own rows unchanged.
- [ ] **S7 Where the time goes.** A measurement of a window repaint and a
  full-screen fill on 86Box's 486 at 1024x768x16, with the `int 10h` bank
  switch counted; the direct `dispi` window register for the Bochs VBE if
  and only if the count says the BIOS call is the cost. Outcome: a report
  under `docs/reports/`, and PERFORMANCE.md Part 5 gains the VESA rows.
- [ ] **S8 Acceptance.** `NOVIDEXT=1` in the build matrix and the measured
  resident cost in SPEC.md §119.1 and docs/KERNEL-MEMORY.md; docs/TESTING.md's
  recipe; docs/INDEX.md regenerated; the deduplicated soak union of every row
  named above run once, then `make test-full` once; hardware-only
  limitations stated (no real VBE card has been booted).

## Decisions already taken, and the alternatives judged

| decision | alternative | why this one |
|---|---|---|
| hooks in `kern_big`, ~one image rung, with `NOVIDEXT=1` as the A/B | a fourth kernel, `kern_vesa` (kern_emu's shape), costing the XT nothing | a VESA user boots the standard disk and the card is found; a fourth kernel is a fourth system disk, a fourth live image and a fourth row in every gate that walks "every shipped image". The owner may still prefer it — the hook is written so that `VID_EXT` can be moved under a build define in one line |
| the desktop stays 16 colours | a 256-colour or RGB desktop | `[gfx_color]` is a nibble in every kernel body and every package; widening it is a rewrite of §5, §6, §25 and the SDK. Depth reaches a package through one slot instead |
| banked window A, 8086 code | the linear framebuffer through unreal mode | unreal mode is 386 code that any segment load undoes, and this kernel reloads segments on every switch (SPEC.md §119.2.3) |
| the driver owns the cursor whole | hooking `vga_save_vram`/`vga_restore_vram` under `cur_*` | a 16bpp cell is 512 bytes against `cur_save`'s 192: the driver's own buffer is where it fits. Its window switch is an `int 10h`, so the mouse ISR's private stack is 256 bytes under `VID_EXT`; a private stack in the driver was refused - two tasks preempted inside one `int 10h` would share it |
| `vx_boot_x` in `.cold` | the blob, beside `vmm_boot_x` | the blob measured 6,142 of 6,144 bytes; 116 resident bytes inside the cold rung's slack until the blob sheds a sector's worth (S8 names it) |
| `DRVC_OVL` with a row, no class | a `DRVC_VIDEO` class with a `DSV_*` table | the kernel is the only caller and holds the far pointer; a class costs a publication slot, a copied table and three per-class arrays for nothing to find |
| `[vid_planes]` = the bpp | a separate depth byte | the three save-under sizers already multiply by `[vid_planes_w]`; at 8 and 16 the formula gives the packed byte count unchanged |
| `gfx_blitp` refused on VESA | translating plane-major bytes to packed | the source is a VGA plane layout; Paint already carries the `gfx_blit4` fallback the refusal routes to |
| hibernate greyed while VESA is primary | switching to VGA inside HIBER.DRV's sweep | `vid_switch` is CTRL.DRV's and the blob's; a third expansion is a decision with bytes attached, taken in S5 or deferred with the arithmetic |
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
