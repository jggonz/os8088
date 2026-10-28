# ksp14 agent "vesa" - the VESA display's resident half

Kernel size pass 14 (the brief is `docs/plans/completed/ksp14/BRIEF.md`).
Concept: SPEC.md 120's resident half - `kernel/vidext.inc`'s `VIDEXT` hook and
every hook site it put in the drawing layer (`vga12.inc`, `font.inc`, the
cursor's `cur_*_1` in `mouse.inc`, `icons.inc`, `viddet.inc`, `vidsel.inc`,
`fsx.inc`, `wm.inc`, the `gfx_blitd`/`OSAPI_GFX_BLITD` cell, `driver.inc`'s
VESA row) and `mou_pstack`'s 128 -> 256 `.lowbss` growth; commits `322dda62`,
re-applied in `553f2a21`. Branch `ksp14-vesa`, base `4be03502` (`26a63034`
plus the brief). Figures are ASSEMBLED bytes, `tools/kernsize.py --json`
absolutes; resident = `.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf`;
driver figures are the assembled image (`build/vesa.bin`).

**The question the brief asked first - what does the hook cost a machine
WITHOUT `VESA.DRV`?** At the base, built `NOVIDEXT=1` against the default:
**875 resident bytes** (`.text` 600, `.cold` 146, `.lowbss` 128, `.bss` 1), on
every kern_big and kern_emu machine whether it has a VBE or not. At the tip it
is **385** (`.text` 358, `.cold` 26, `.bss` 1), the `NOVIDEXT=1` arm
assembling to exactly the base's `NOVIDEXT=1` sizes. Most of what went was not
VESA code at all but the SHAPE of the hook: four trampolines, a byte that
repeated what `[vid_planes]` already said, a 1bpp test sitting in front of
every hook's own compare, a stack doubled for every machine, and a boot-only
attach and its pointer table resident for life.

## BASE AND TIP

| | base | tip | delta |
|---|---:|---:|---:|
| **kern_big resident** | 93,342 | **92,852** | **-490** |
| kern_big `.text` / `.bss` / `.cold` / `.lowbss` / `.vgabuf` | 44,005 / 5,375 / 38,140 / 5,486 / 336 | 43,763 / 5,375 / 38,020 / 5,358 / 336 | -242 / 0 / -120 / -128 / 0 |
| kern_big `KERN_SIZE` | 99,328 | **98,816** | **-512** (one image rung) |
| kern_big `.ovl` | 5,118 | 5,118 | 0 |
| kern_big blob (`boot2_end`, the real one) / `BOOT2_SECS` | 6,137 of 6,144 / **12** | 6,307 of 6,656 / **13** | +170 bytes, +1 sector |
| kern_big `.boot2` (kernsize's NOKZIP figure) | 5,926 | 6,107 | +181 |
| **kern_emu resident** | 93,611 | **93,121** | **-490** (`.text` 44,274 -> 44,032, `.cold` -120, `.lowbss` -128) |
| kern_emu blob / `BOOT2_SECS` / `KERN_SIZE` | 6,226 of 6,656 / 13 / 99,328 | 6,407 of 6,656 / **13** / 99,328 | +181 bytes, sectors unchanged |
| **kern_small resident** | 59,207 | 59,207 | **0** - `os88rdiff -D KERN_SMALL`: 2,581 routines same, none shifted; `BOOT2_SECS` 11 either way |
| **kern_mega resident** | 103,165 (+ `.mega` 14,698) | 103,165 (+ 14,698) | **0** - every section the same size; `boot2_entry`, `mod_map`, `mem_init_x` differ only by the blob length, which is 13 -> 14 sectors (it is a `KERN_KNOB` build: see REFUSED) |
| `VESA.DRV` image | 7,873 | 7,903 | +30 (8 KB claim either way, 289 to spare) |

**What the concept still costs a non-VESA kern_big: 385 bytes**, against the
base's 875 - so this pass took **56%** of it. What is left is the feature
(the hook compares, `vid_tab`'s row, the driver row and its strings, the
`OSAPI_GFX_BLITD` body, the fsx and Display answers, `vx_null`).

## TAKEN

All of it is the concept's own code unless marked. Cycle counts are 8088
clocks (EA 6 for a direct address), not-taken `jcc` 4, taken 16.

### 1. The hook is the far call itself - `.text` -80 (kern_big, kern_emu)

`VIDEXT` was `cmp / jz / call vx_go / db n / db 0`, and `vx_go` rebuilt its
near return into a far pointer to the selector, far-called `[vx_ep]`, popped it
back and returned; `vx_call` did the same and jumped past the byte;
`vx_callf1` was the same again for `.cold`'s `gfx_blit1_x`; and **`vx_goc` was
dead** - nothing called it (SPEC.md 120.3.1 still documented it as
`gfx_blit1_x`'s). 118 bytes of trampolines and two shared scratch words.

Now every hook is `call far [vx_ep] / db n`: **the far return the call pushes
IS the selector's address**, so the driver reads the byte through its own
return address and no kernel code stands between. **Bit 7 of the byte,
`VX_CONT`, says where the driver goes back to**: set, past the byte (a hook
inside a frame); clear, to `VXB_KRET`, a kernel `ret` (`vx_kret`) that pops the
hooked body's own return - so a `VIDEXT` body returns to its caller exactly as
`vx_go` made it. The driver rewrites its far return BEFORE the body runs, so
nothing after the body touches a flag and its CF answer is untouched.
`VIDEXT` stays twelve bytes with no parity pad; `VIDEXTC` stays fourteen.
Hand-written sites: `VXGO n` / `VXCALL n`; `gfx_blit1_x` far-calls `vx_b1`
(`VXCALL VX_BLIT1 / retf`, six bytes in `.text`, `.cold` carrying no data).

**The guard had to stay and it is now a destination**: `vx_go` tested
`[vx_ep+2]` before every call, and that IS reachable - `drv_shutdown_x`
detaches every driver while `[vid_kind]` is still `VID_VESA`, and the arrow
(mouse ISR), `cur_unlazy` and `vid_setmode` (CTRL.DRV's shutdown) all run
after. The detach now points `vx_ep` at **`vx_null`** (23 bytes, `VXB_NULL`),
which keeps the driver's exit protocol and draws nothing: on past a `VX_CONT`
byte, through `vx_kret` otherwise, CF = 0 (ZF = 1 on the tail, as the guard's
`cmp` left it). `VX_ABI_VER` 4 -> 5.

Hot paths: a non-VESA machine's hook is the same `cmp`/taken `jcc` as before.
A VESA machine loses the trampoline (~120 clocks a primitive) and 6 bytes of
mouse-ISR stack per cursor call.

### 2. `vx_boot_x` and `vx_blk` into the boot blob - `.cold` -120, `.text` -58 (kern_big, kern_emu)

`vx_boot_x` (attach + `VXV_INIT` + the switch) runs once, from `kmain_o`, which
is blob code; it was `.cold` only because the blob had two bytes left when it
was written (its own header said so and named the move). `vx_blk`, the 58-byte
table of kernel pointers, is read by `VXV_INIT` alone and the driver copies it,
so it is dead the moment that call returns. Both are `.boot2` now,
`BLOBCALL vx_boot_x`, and `VXV_INIT` takes the block as **DX:SI** (DX = the
blob's segment; ES stays the kernel's, which the driver banks).

**`BOOT2_SECS` 12 -> 13 on kern_big** (the brief allows a sector):
`KSIG_OFF` 4608 -> 4096, file sector 21 on all four geometries
(`t_canary`: word distinct from both neighbours). **`tests/unit/t_blobruns.py`
prices it at no extra `int 13h` on any geometry - and it FIXES that row**,
which fails at the base: the kernel had grown past a cylinder so stage 2's read
on 360KB and 720KB was 12 calls against a ratchet of 11, and the thirteenth
sector puts it back to 11. **kern_emu's extra sector goes**: it was 13 for
`vmm_boot_x` alone, and with both attaches its blob is 6,407 of the same 6,656,
so it is on kern_big's literal and `KSIG_OFF` now (SPEC.md 9.11.2.1 says so).
kern_small keeps eleven (`BOOT2_SMALLSEC` 2). `tests/ovlrefs.txt` loses the one
cross-blob registration it carried for this; `tools/os88ladder.py`'s tables
name the phase (its `--selfcheck` refused the tree until they did).

### 3. The mouse ISR's stack back to 128, over the row table - `.lowbss` -128 (kern_big, kern_emu)

`MOU_PSTK` was 256 under `VID_EXT` because a VESA arrow does an `int 10h` (a
window switch) on the mouse ISR's stack: 128 bytes of `.lowbss` on every
kern_big, most of which never runs a VBE. The brief's warning was found and
kept - the 256 is not trimmed, the VESA arrow gets MORE: `vid_rowtab` (696
bytes) is now declared directly under `mou_pstack` on a `VID_EXT` build
(`VID_ROWTAB_MOU`, asserted adjacent at assembly), and **nothing reads a row of
the table while the display is packed** - every body that would use one is
hooked (`tests/vidext.txt` is the inventory; `[vid_rowmax]` is still armed but
no VRAM write goes through a row on that kind). So the arrow's stack may run on
into it: **824 bytes for a packed display, 128 for every other kind**, which is
`NOVIDEXT=1`'s and every pre-VESA kernel's figure (2.4x the 54-byte chain).
`vid_apply` rebuilds the whole table when the desktop leaves `VID_VESA`, and
`vid_depth_set` runs before that rebuild, so a deep ISR cannot follow it (the
ISR runs IF = 0 to its `iret`). `.ovl`'s landing ceiling is independent of what
follows the mount buffers (kernel.asm's own `%if`).

### 4. The hook's test is the depth - `.text` -22

`[vid_ext]` (a `.text` byte plus a parity byte, and two stores in
`vid_depth_set`) said exactly what `[vid_planes]` says: `vid_depth_set`
publishes the bpp, 8 or 16, as the plane count on `VID_VESA` and 4 or 1 on
every other kind, in the same word store as `[vid_mono]`. Every hook now tests
`cmp byte [vid_planes], 4 / jbe planar` - the same five bytes and clocks - and
the byte is gone. (`vx_bpp` is 8 or 16 by the attach answer's contract; nothing
but `vid_depth_set` writes `[vid_planes]`, which `t_invariants` already
guards.) `tests/vesa.py` and `vesa8.py` read `vid_planes` and no longer the
byte. Before that: **`vid_depth_set`'s VESA arm shares the store tail** (-9),
and `vx_class`'s parity pad went with it (-1).

### 5. One compare for 1bpp, planar and packed - `.text` -82 over fifteen bodies

Eight planar bodies opened `cmp [vid_mono], 0 / jne mono` and then `VIDEXT`'s
own compare; `[vid_planes]` is 1, 4 or the depth, so **`VIDEXTM n, mono`** is
both in `cmp [vid_planes], 4 / jb mono / je planar / call far [vx_ep] / db n`:
`ico_core`, `cur_put_1`, `cur_get_1`, `cur_move_1`, `gfx_fill_gray_raw`,
`gfx_fill_pat_raw`, `vga_save_vram`, `vga_restore_vram` (-38 with two parity
pads). The same fold, spelled per body: `gfx_fill_raw` and `gfx_xor_fill`'s
`.arm` test the depth on the way into `vga_solid_rect`, whose `VIDEXTF` reads
the compare's flags (-4); `font_run_x` jumps to `.planar` on `jae` and
`.planar`'s `je` reads the same compare (-6); `gfx_spans`' packed arm joins the
dispatch below the row base (-12, the row base computed and unused on a packed
display); `gfx_blit4` (-6), `gfx_scroll` (-4) and `font_char` (-4) the same.
And `gfx_blitp`'s refusal of a 1bpp AND a packed display is
`cmp [vid_planes], 4 / jne .no` (-8: `VIDEXTR`'s seven bytes and a pad).
Without the hook every macro is the original two lines, byte for byte, so
kern_small, kern_mega and `NOVIDEXT=1` assemble as before.

**Every one is faster on VGA and costs a 1bpp display nothing**: a VGA body
loses a memory compare and a taken jump (~32 clocks: `cur_put_1`/`cur_get_1`/
`cur_move_1` under the mouse ISR, `gfx_fill` - the most-called drawing
routine - `font_run`, `font_char`, `gfx_blit4`, `gfx_scroll`, `gfx_spans`,
the icon and save-under bodies); a 1bpp body pays the one compare and the one
taken jump it always paid. Hot-routine entry parity is pinned (`hotpar`
green); the pads are never executed.

## REFUSED

* **A driver-private stack for the VESA arrow** (instead of item 3): SPEC.md
  120.3.3 already refused it - two tasks preempted inside one `int 10h` would
  share it - and there is a second reason: `sch_switch` saves SP alone (SS is
  LOW_SEG for every task), so a task preempted on a driver-segment stack comes
  back on the wrong one.
* **`MOU_PSTK` trimmed on an estimate** - not done: item 3 gives the VESA path
  824 bytes, more than its 256, and leaves every other kind on the measured
  128. **Measured on QEMU** (a scratch probe: the 824 bytes filled with a
  sentinel through the gdb stub on a `VID_VESA` desktop, the pointer driven
  across the screen 67 times, the lowest byte written read back): the VESA
  arrow went **139 bytes** below the stack's top - past 128, 11 bytes into the
  row table, which is exactly what item 3 is for, and the desktop drew on
  unharmed. That is SeaVGABIOS's `int 10h`; a real card's VBE BIOS may be
  deeper, and 824 is the room it has.
* **Self-modifying hook sites** (a patched `jmp short` over the far call, armed
  at attach): ~5 bytes a site and faster, but `[vid_planes]` changes at every
  Set Primary and display swap, so the patch table and its loop would be
  resident (~55) for a net ~-20; and `make rom` hashes `.text`.
* **`vid_ext` derived from `cmp [vid_kind], VID_VESA`** (the first spelling of
  item 4): `[vid_kind]` moves in `vid_switch` BEFORE `vid_apply`, so an arrow
  drawn in that window would call the driver on a card still in mode 12h.
  `[vid_planes]` moves in the same store as `[vid_mono]`, which is why item 4
  takes it.
* **`wm_display`'s and `vid_disp_planes`' `[vx_bpp]` as `[vid_planes]`**: the
  latter is the ACTIVE display's, and both answer about a display that may not
  be.
* **`fsx_capstab`'s VESA row** (2): `fsx_caps` maps the kind to `VID_VGA`
  first, but `fsx_mode` indexes the table by `[vid_kind]` directly.
* **`vid_tab`'s VESA row into the driver** (18): `vid_apply`'s copy would need
  a far arm longer than the row.
* **`VXB_KRET` onto an existing `ret`** (-1): odd, and it would need a pad.
* **`gfx_blit1_x` calling the driver directly**: `.cold` carries no data
  (SPEC.md 2.6 rule 1, `os88ovlchk`), so the selector byte cannot follow the
  call; `vx_b1` is that byte's home.
* **`VESA.DRV` to a 7 KB claim**: file + bss is 7,903 against 7,168.
* **kern_mega's thirteenth sector**: kern_mega is a `KERN_KNOB` build, so it
  takes the knob's extra sector and reads 14 where 13 would hold its 6,482-byte
  blob - one in-run sector (~24 ms), no extra `int 13h` on its 1.44MB or 1.2MB
  disk (t_blobruns' arithmetic). Exempting it means a `KERN_MEGA` arm in
  `BOOT2_KNOBSEC`, the Makefile's `BOOT2_XSEC` and `boot2.asm`'s `KSIG_OFF`, and
  a mega knob build would then need its own; kern_mega is not this pass's
  target (BRIEF 1). The same holds for every knob build, 13 -> 14.

## WANTS THE OWNER

1. **`vx_ep` follows a moved `VESA.DRV` image** - `.text` **+4** on kern_big
   and kern_emu: `MEM_RR_ONE vx_ep + 2` in `memory.inc`'s `mem_rr_tab` under
   `VID_EXT`. A DEFECT FIX (below), so it changes behaviour on that path: it is
   put rather than taken.

   **TAKEN at the owner's word** by the coordinator after the merge: kern_big
   92,594 -> 92,598, as priced. The owner notes another session has reworked
   heap compaction in parallel, so this row is to be REVIEWED when that work
   and this pass merge.

## WANTS AN ABI BREAK

None. `VX_ABI_VER` 4 -> 5 is the kernel <-> `VESA.DRV` private contract
(`kernel/vxabi.inc`, shared as source; the image refuses another vintage at
attach), not `PKG_FMT`/`DRV_VER`; `OSAPI_GFX_BLITD`'s cell is untouched.

## CROSS-FILE

* **`fprog.inc`'s `fpg_hide` text-band erase** (+28 since pass 13, menubar's
  file): it runs on every kern_big, a second `fpg_span` and a compare per hide,
  for a bar only `VID_VESA` makes wider than `MENU_BCMAX` cells. Gating it on
  `[vid_planes] > 4` costs bytes rather than saving them; noted so the menubar
  agent need not re-derive it.
* **`menu.inc`'s `menu_s_hibervx`** (18, `'Hibernate (VESA)'`): the three
  greyed Hibernate labels share an 11-byte head, but a composed label costs
  more than it saves. Nothing to take.
* `hb_ok_x`'s VESA refusal (`.cold`, 10) is already minimal.

## DEFECTS FOUND

* **`vx_ep` is not in `mem_rr_tab`, so a compacted `VESA.DRV` image is called
  at its OLD address** (by reading; not reproduced). Driver images move
  (SPEC.md 66.6.3): `mem_can_move`'s `.drvimg` arm asks three facts, and
  `mem_region_reloc` patches every word in `mem_rr_tab` - `drv_tab`'s
  `DRVR_SEG` among them - but `vx_ep`'s segment half is in none of its rows.
  `VESA.DRV` loads at boot as the LAST driver row, so it sits lowest in the
  ceiling's driver stack; unload one loaded above it on the Drivers page and
  the next compaction can move it up, after which every hook far-calls freed
  memory - it works until that memory is reused. The fix is WANTS THE OWNER 1,
  +4. (Both before and after this pass: the hook's shape does not change it.)
* **A VESA desktop's Shut Down draws no "safe to turn off" screen** (by
  reading; not reproduced, and not this pass's to change). CTRL.DRV's
  `modc_e_shutdown` calls `drv_shutdown_x` FIRST, which detaches `VESA.DRV`
  while `[vid_kind]` is still `VID_VESA`; its `vid_setmode` then reaches the
  VESA arm and the null entry (the old guard did the same), so the card stays
  in its VBE mode with the desktop on it, `sd_text`'s `font_run` draws nothing,
  and `sd_spin` - a splash-style writer, unhooked - writes planar bytes into a
  packed window. The shape of a fix is `ui_cmd_reboot`'s and the Drivers
  page's own: put the desktop back on the VGA (`vid_switch AL=VID_VGA`, which
  is CTRL.DRV's) before the detach. A functionality change either way.
* **`tests/unit/t_blobruns.py` (soak) fails at the base**: stage 2's kernel
  read is 12 calls on 360KB and 720KB against a ratchet of 11. Item 2's sector
  puts it back to 11 (green at the tip).
* **`tools/os88ladder.py --selfcheck`** would have refused the tree after item
  2; fixed with it.
* **The `vesa` row fails at the base, identically**: *"the disk widget's
  chrome is still on the bar after the transfers: 8 non-white pixels between
  x=600 and 860, first at (756, 6)"* - run on a worktree of `4be03502`, same
  pixel. It is the `menubar` agent's defect and FIXED on its branch
  (`f3be393d`: on a 1024-wide bar `MENU_BCMAX` was 85 cells and the clock
  starts at cell 98, so every bar composition overran `menu_bcell`); this
  branch changes neither the bar's width nor its cell count, so the row is
  expected red here until `kernel-size-14` is merged in. `viddet.inc`'s
  `vid_apply` comment quoted `MENU_BCMAX = 62`; it now names the constant
  rather than a figure.
* **`vesagor` and `vesasu` declare no `needs=("qemu", ...)`**, so the runner
  schedules them as host rows beside the QEMU ones, and they share
  `tests/vesa.py`'s fixed `build/vesa.sock` and pidfile - `kill_stale` kills
  the other row's QEMU. Seen here as `vesagor` failing in 0.9s with no socket;
  alone it passes (ROWS RUN). Not fixed (a row's registration, not this
  concept's code).
* **SPEC.md 120.3.1 documented `vx_goc` as `gfx_blit1_x`'s entry**; the code
  used `vx_callf1` and `vx_goc` was dead. SPEC.md now describes what is built.
* SPEC.md 120.4 said `[vid_rowmax]` is cleared on a packed display; it is not
  (the table is built with that kind's stride and never read). Harmless, and
  item 3 now relies on "never read", which `tests/vidext.txt` enforces.

## ROWS RUN

`python3 tools/os88test.py soak --marty-jobs 1` over 39 rows, ONCE, after the
last code commit: **37 passed, 2 failed** (1,075 s).

* QEMU, the VESA display: `vesa8`, `vesacp`, `vesablitd`, `vesablit4`,
  `vesafsx`, `vesapref`, `vesasu`, `mixervesa` - ok. **`vesa` FAILED and fails
  identically at the base** (DEFECTS FOUND: the menubar agent's `MENU_BCMAX`
  overrun, fixed on its branch). **`vesagor` FAILED in 0.9 s on a missing
  socket** - the scheduling defect above; run alone afterwards: **ok**.
* MartyPC, the drawing bodies item 5 rewrote, on CGA, Hercules and VGA:
  `gfxpoints`, `dispblit`, `dispcheck`, `dispdepth` (VGA beside Hercules),
  `disptitle`, `dispthm`, `dispherc1`, `dispmode`, `dispsave`, `dispblitp`,
  `dispseam`, `dispstrad`, `disptext`, `curshape`, `curbusy`, `curdisk`,
  `icoclip`, `fontpick`, `paint1blit`, `blitp`, `blitplane`, `blitcut`,
  `mousess`, `su1bpp`, `xorrect` - ok.
* The boot (item 2): `blobruns` (red at the base, green here), `blobsum`,
  `kzboot`, `dispreboot` - ok; `canary` and `hotpar` in the fast tier at every
  batch.
* **Broken on purpose**: `VESA.DRV`'s tail redirect (`mov [bp+2], bx` to
  `VXB_KRET`) replaced with a `nop`, so a `VIDEXT` hook falls into the planar
  body on a packed display - `vesa8` red (*"the dock's band is 0% non-black;
  the desktop at (400, 300) is black, want the 50% dither"*); restored, green.
* Every batch: `make -j2` (the fast tier, 61/61), `make -j2 small`, `make -j2
  emu`, `make -j2 mega`; `tools/stkbalance.py` over every file touched, base
  against tip - the same counts (0 in `vidext.inc`, `viddet.inc`,
  `vesa.asm`; the pre-existing 11, 1, 6 and 2 in `vga12.inc`, `font.inc`,
  `mouse.inc`, `icons.inc`); `tools/os88ladder.py --selfcheck` ok;
  `NOVIDEXT=1` assembles to the base's `NOVIDEXT=1` sizes exactly.

Also, by hand in QEMU (a scratch probe, not a row): `build/vesa.img` booted to
`VID_VESA`, `[vx_ep]` poked to `vx_null` through the gdb stub (what the detach
now does), the pointer driven across the screen and a menu dropped and
released - every cursor hook (tail), fill (tail) and text run (`VX_CONT`) went
through `vx_null`: the guest kept running, the pointer tracked to the pixel,
and the screen did not change by one pixel; `[vx_ep]` put back, the next menu
drew and the desktop came back to within the arrow's 128 pixels.

## METHOD NOTES

* `os88rdiff --lst` against a `-DNOVIDEXT` listing of the SAME tree is the
  instrument for "what does the hook cost a machine without the driver": it
  names every routine the hook touched, which the concept table (a diff across
  commits) cannot separate from the rest of the merge.

## FIXED AT THE OWNER'S WORD: a VESA desktop's Shut Down screen

The owner asked for DEFECTS FOUND's *"A VESA desktop's Shut Down draws no
'safe to turn off' screen"* to be fixed if it was easy (*"If its easy, again,
can do it"*). Branch merged up to `kernel-size-14` (`3e98b6b1`, a fast-forward)
first.

**The fix**, in CTRL.DRV's `modc_e_shutdown` (`kernel/shutdown.inc`) and
under `VID_EXT` only: just before the screen's `vid_setmode`, a
`cmp byte [vid_kind], VID_VESA / jne`; on a VESA desktop `[vid_kind]` becomes
`VID_VGA` and THIS image's `vid_apply` (the panel's, `VID_APPLY vid, KSWK` in
`.modu`) publishes the VGA's geometry and depth, so the `vid_setmode` that
follows sets the card's own mode 12h and `sd_text`/`sd_spin` draw on it.
`VESA.DRV` is already gone at that point (`drv_shutdown_x`), which is the
reason the kernel's own mode is the only one left to draw on; nothing is
reordered, and the desktop is not repainted first (a whole `vid_switch` would
have been - menu bar, refit, a paint nobody sees).

* **Bytes: 0 resident** on every kernel. `CTRL.DRV` (an on-demand module)
  14,889 -> **14,904** (+15) on kern_big, the same +15 on kern_emu, a 15 KB
  claim either way; kern_small (no `VID_EXT`) and kern_mega assemble byte for
  byte as before (`os88rdiff`: every routine the same).
* **Behaviour elsewhere is unchanged**: on a VGA, CGA, EGA or Hercules desktop
  the arm is one compare and a taken `jne`, in a module, once per Shut Down.
* **Rows** (one scoped run, `soak -k 'vesa*' -k mixervesa -k shutdown`):
  `shutdown` (MartyPC: VGA, CGA and Hercules) ok; `vesa`, `vesa8`, `vesacp`,
  `vesablit4`, `vesafsx`, `vesapref`, `vesabench`, `mixervesa` ok; **the new
  row `vesashut`** ok - System > Shut Down... and Enter on `build/vesa.img`,
  asserting a 640x480 screen, `vid_kind` 0 / `vid_planes` 4 and the sentence's
  white pixels. **Broken on purpose** (the `jne` made a `jmp`): the screen
  stays 1024x768, `vid_kind` 4, red; restored, green.
  `vesagor` failed in 0.3 s in that run - the shared-socket scheduling defect
  above - and passes alone. **`vesasu` and `vesablitd` FAIL, alone, and fail
  identically on a worktree of `kernel-size-14`'s tip `3e98b6b1` without this
  change** (*"the card's format drop-down has no rect: Save As did not open"*;
  *"0 distinct non-EGA colours on the glass: the canvas is not at depth"*).
  Both passed on this branch before the merge, so something that arrived with
  `kernel-size-14` broke them; not this fix, which neither row reaches.

