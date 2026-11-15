# Driver size pass 1: vesa (VESA.DRV, XMEM.DRV, EMS.DRV, USBMOUSE.DRV, VMMOUSE.DRV)

Branch `dsp1-vesa`. Brief: [BRIEF.md](BRIEF.md). The base is `de93581a`, which
is `3b16b60b` plus the brief. Measured in this worktree.

## 1. BASE AND TIP

"image" is what RAM pays: the claim is the image rounded up to 512 bytes.
"file" is what the disk pays. Clusters are on the 360KB system disk
(`os8088-360.img`, 512-byte clusters). XMEM.DRV is not on that disk
(`$(SYS360OMITDRV)`), and VMMOUSE.DRV is on the emu disks only.

| driver | image base -> tip | bss base -> tip | claim base -> tip | file base -> tip | 360KB clusters |
|---|---:|---:|---:|---:|---:|
| VESA.DRV | 8,105 -> **7,406** (-699) | 416 -> 224 | 8,192 -> **7,680** (-512) | 3,848 -> **3,536** (-312) | 8 -> **7** (-1) |
| XMEM.DRV | 2,037 -> **2,015** (-22) | - | 2,048 -> 2,048 | 1,878 -> **1,864** (-14) | (not on it) |
| EMS.DRV | 785 -> **783** (-2) | 32 -> 32 | 1,024 -> 1,024 | 717 -> **715** (-2) | 2 -> 2 |
| USBMOUSE.DRV | 1,521 -> 1,521 | - | 1,536 | 1,423 -> 1,423 | 3 -> 3 |
| VMMOUSE.DRV | 375 -> 375 | - | 512 | 375 -> 375 | (emu disks) |
| **total** | **-723** | | **-512** | **-328** | **-1** |

`os88diskuse --diff` against the base: `os8088-360.img` free clusters
37 -> 38. No other 360KB image changed. The kernel moved by one constant
(see section 6), so KERNEL.SYS changed by bytes and not by clusters.

## 2. TAKEN

All of these were checked against the base bytes with a unicorn harness
(`vxh.py`, scratch). It runs a model VBE BIOS: 4F00h/4F01h/4F02h/4F05h,
banking through a 4MB virtual framebuffer, a BIOS that scrambles SI/DI on
4F01h, a list placed inside the 4F00h buffer, and a list longer than 64
entries. It runs ATTACH, INIT, and for all 256 class bytes VXV_NEXT and
VXV_PICK, over seven mode sets. These include the odd-granularity, refused,
16-mode-limit and tie cases. It then runs every drawing body, 17 selectors
over 6 modes, with window-straddling rects, cursor cells, scrolls, blits and
spans. It records registers, frame outputs, kernel cells, int 10h calls and
SS:SP depth, ports, and a hash of VRAM. I mutation-tested it: two one-byte
breaks went red in 1,700 places. Result: tip = base, **plus the two defect
fixes** in section 9 (checked as tip = base-with-the-two-fixes), and except
for the undefined byte in section 9.3.

### VESA.DRV (image -699, file -312)

| item | image | hot? | how equivalence was shown |
|---|---:|---|---|
| `vx_attach` + `vx_offer` move into `vx_once` at the end of the code. `vx_attok` (resident) zeroes them and makes them the post-attach int 10h stack: VX_STK = 898 bytes, the same depth the probe's 768-byte buffers plus the dead 130-byte list copy gave before. While attach runs, the 4F00h buffer is the zeroed tail of the region. The 4F01h block and the list copy sit at the foot of the scroll bounce, and the probe's int 10h hops onto its top (1,662 bytes; Bochs was measured at about 650). `vx_entry` admits ATTACH only while `[vx_nmodes]` is 0: a success leaves it at 1 or more, and a refusal frees the image. | about -290 | cold | harness (stack depth at every int 10h, the zeroed headroom under the new top is 898, every attach answer); read the region and every jump out of it |
| Mode record in the mode-info block's own order (BPSL,X,Y; RSIZE..BPOS), so `vx_offer` appends with two `movsw` runs. Granularity, depth/model and range tests tightened. | -113 (offer + attach) | cold | harness, every scenario |
| `vx_mkpal`: reads (size,pos) as one word; its loop is `.field` called three times | -27 | cold | harness, palette in every pick |
| `vx_init` (`lodsw` the version; drops two dead stores, `[vx_win]`/`[vx_cursaved]`, which are their load values there), `vx_geom` (`stosw`) | -38 | cold | harness, vid_tab row and vx_ep+4/+6 |
| `vx_publish` (`lodsw`; `vx_depth`/`vx_wsh` one word in state), `vx_choose` (the pick in DI, area in CX:BX; `vx_a1` gone), `vx_decode` (`xlat`), `vx_encode` (`.idx` takes AX, AH counts), `vx_find` (compares `vx_encode`'s answer with CL & 9Fh), `vx_next` (`mul byte`, `.firstc` through `.first`, `vx_tmpc` gone), `vx_pick` shares `vx_init.stc` | about -90 | cold | harness, 256 classes x 7 sets for NEXT and PICK |
| `vx_prim` dispatch in registers (`vx_sel` gone) | -15 | per primitive; FASTER | 8088 table count 169 -> 102 cycles; 386 40 -> 29; harness |
| `vx_clip` computes `[vx_rows]`/`[vx_w]` for its four callers (fill, gray, pat, sr_setup): the same six instructions, run once, and its `clc` is gone (the `sub` leaves CF = 0); `vxp_pat` drops its `push cx`/`pop cx` | -45 | per primitive; FASTER (-2 cycles, one push/pop fewer in pat) | read; harness |
| `push bx`/`pop bx` around `vx_addr` (which keeps BX) dropped in 15 row/pixel loops (fill, gray, pat, save, restore, fontchar, icon per pixel, cursave, currest, curdraw, blit4, blitd, blit1, scroll, fontrun) | -30 | per row / per pixel; FASTER (-27 cycles each on an 8088) | read each loop body for BX; harness |
| `vx_rowfill` 8bpp: three dead `mov ah,al`/`mov al,ah` dropped; one `push bx` for both arms | -7 | per row; FASTER | harness |
| `vxp_refuse` (an unused label: `t_vidext` checks that it is NOT in `vx_jt`) | -2 | - | assembled; t_vidext green |
| `vx_rowread` (`vx_rowcopy` with the edge being the source's). VX_SAVE at full depth and both halves of VX_SCROLL use it or `vx_rowcopy` where they moved a byte at a time with VXW/VXWS: the same bytes and the same window switches (an edge that ends the row included), at `rep` speed | -30 | per row; FASTER (8bpp: 54 cycles/byte -> about 8.5/byte + about 70 a row) | harness, straddling saves and scrolls on 800x600x16 |
| fixes: spans width, 16bpp straddle (section 9) | +/-0 | per row; neutral or faster | probes before/after |

### XMEM.DRV (image -22, file -14)

`xm_hma` banks only BX, DX and ES, which is what `xm_attach` holds across it.
It far-calls COLDHMA.SYS through a pointer pushed on the stack: rom_patch
"preserves every register" and takes nothing in (boot/osrom.asm). `xm_arm`
drops an AX save that nothing reads. `xm_ltrip`'s `.leg` banks SI instead of
flipping it back under pushf/popf. All cold (attach). Equivalence was shown
by reading: the stack layout of the far call, CF across the pops, and every
register `xm_attach` uses after each call. The once region's code went from
221 to 207 bytes against the 208-byte scratch, so the scratch is the bound
now and further region trims buy nothing. SPEC.md 41.12.6 carries the
figures.

### EMS.DRV (image -2, file -2)

`em_alloc`'s two failure codes share one tail through `cmp ax, imm16`
(EMSE_* < 128, so `cbw`). Cold. Shown by reading.

## 3. REFUSED

- **EMS `em_fsel` above OS88_STATE**: the `os88_state` contract ("zero-only
  below") is broken by `em_fsel` living under it. Moving it costs **+4 bytes
  of file**, 0 of image. Refused: the file grows. tests/vidrmbems.py reads
  `os88_state` and is fine either way.
- **VESA: one shared clip prologue for `vxp_blit4` and `vxp_blitd`** (about
  45 bytes). It adds a call/ret to each primitive, so it is slower. Refused.
- **VESA: the arrow's save-under and restore through `vx_rowread`/`vx_rowcopy`**
  (about -24 bytes, and faster). Refused: on the mouse ISR's private stack
  it reaches `vx_nextwin` 6 bytes deeper, in the case where an int 10h is
  already out on the image's stack (SPEC.md 120.3.3's 824-byte budget).
- **VESA: `vx_dac16` generated at attach**: 0 image bytes (the 48 bytes stay
  as state), about -20 file. Not worth the code.
- **VESA: store each record's class byte at offer time** (`vx_find` about
  -20). Refused because it changes the undefined answers in section 9.3.
- **USBMOUSE, VMMOUSE**: read every routine. Nothing found that is both
  smaller and not slower. Both have been through earlier size passes
  (USBMOUSE's attach code is already its buffer; VMMOUSE is 386 code with
  no slack). The few 1-2 byte reshapes I priced all cost a flag or cycles
  on a poll path.
- **XMEM: the block table stride 8 -> 6**: 0 bytes, because the image is
  bound by the region's code and scratch together.

## 4. WANTS THE OWNER

- **VESA VX_STK 898 -> 768**, the depth SPEC.md 120.3.4 documents. This is
  **-130 image bytes** (still 15 granules). It lowers the int 10h stack's
  real headroom to the documented figure.
- **VESA `vx_offer` should refuse a mode the class byte cannot name**
  (X not in 640/720/800/1024, or Y/8 not in 50/60/75/96, e.g. 960x720,
  768x576, 1024x576). About +6 bytes. It changes which modes are offered
  and what Auto picks on such a card. See section 9.3.

## 5. WANTS AN ABI BREAK

None.

## 6. CROSS-FILE

- **kernel/driver.inc `DRVM_IMG_VESA` 16 -> 15. I EDITED IT, outside my
  directories**: `tests/unit/t_drvmem.py` (fast tier) mirrors the image's
  512-byte claim there. VESA.DRV crossed it, and `make` is red without the
  change. It is one equ for the Drivers page's memory column, kern_big's
  kernel. Please confirm at merge. It sits in the same block as every
  other agent's `DRVM_IMG_*` line, so expect neighbouring edits there.
- **DRV_H_KEEP for overlays**: the kernel applies a driver's tail-shrink
  only in `drv_attach`'s class path. `vx_boot_x` and `xm_boot_x` attach
  their overlays themselves and never read it. Priced for my drivers, it
  buys no granule anywhere. VESA's attach-only code is now overlaid, with
  30 bytes left resident in `vx_attok`. XMEM's resident attach-only code is
  `xm_lainit` + `xm_ltrip` + `la_capture`, about 285 bytes; 2,015 - 285 is
  still a 2KB claim. EMS's `em_attach` is 197 bytes, and 783 - 197 = 586 is
  still 1KB. USBMOUSE's is already reused as its buffer.
- **docs/plans/KERN-MEGA-PLAN.md 14.115** still says "the probe's 768 bytes
  of buffers". That is a plan record, so I left it. SPEC.md 120.3.4 is
  updated.
- **SPEC.md 9.11** quotes VMMOUSE.DRV at 357 bytes; the base builds 375. I
  did not touch the driver, so I did not move the figure. It is a stale doc
  figure for whoever owns section 9.
- `apps/os88la.inc` / `apps/os88ub.inc` (XMEM includes them) were not read
  for bytes. They are shared with the Video Player and VIDDISK.

## 7. ROWS TO RUN

- VESA: `vesa`, `vesa8`, `vesacp`, `vesashut`, `vesablitd`, `vesablit4`,
  `vesafsx`, `vesapref`, `vesagor`, `vesasu`, `vesabench`, `mixervesa`,
  `megatree`, and the kern_mega ports `mega-vesa`, `mega-vesa8`,
  `mega-vesacp`, `mega-vesablit4`, `mega-vesafsx`, `mega-vesashut`.
  Fast tier: `vidext`, `drvmem`.
- XMEM: `xmcheck`, `xmla`, `xmhwalk`, `hma286`, `hmacold`, `rdxms`,
  `msegxms`, `dosxmsq`, `vidxms`, `vidxmsbios`, `vidrmb286`, `vidrmbfrag`;
  full tier `megapkg`.
- EMS: `ems`, `emspico`, `emscard`, `emscard2`, `videms`, `videmshyb`,
  `videmsslow`, `videmswrap`, `vbtems`, `vbtemsslow`, `vbtemsnone`,
  `vidrmbems`, `syscheck`.
- The `DRVM_IMG_VESA` kernel byte: `drvmem` (fast), plus anything that boots
  kern_big.

## 8. LOOKED

No boot. The coordinator's wrap-up came before the look-boot slot, and the
harness covers the bodies. QEMU `vesa`/`vesacp` are the rows to look with.

## 9. DEFECTS FOUND

1. **FIXED: VX_SPANS drew nothing or a single pixel a row.** `vxp_spans`
   computed the width in DX, called `vx_addr` (which spends DX on the window
   number), and then moved DX into CX. So `gfx_spans` (Paint's filled
   shapes, isoTron's sweeps) on a VESA desktop filled 0 pixels in window 0
   and N pixels in window N. I found it by reading `vx_addr`'s clobbers and
   confirmed it in the harness (base: 0-2 bytes changed a span row; fixed:
   the whole span). The fix moves the width into CX before the call, which
   is smaller and faster.
2. **FIXED: 16bpp fills lost a window edge.** `vx_rowfill` formed DI + 2*CX
   as `DI + CX + CX`, so the carry of the first add was lost when more than
   half the run lay past the edge. The run then wrapped onto the first
   bytes of the same window. This hits 800x600x16 and 640x480x16, whose
   strides do not divide 64KB. Harness: a fill of x 750..799 on row 40 at
   800x600x16 wrote row 0's x 0..31. The fix forms 2*CX first, with the
   same instructions in a different order: cycle-neutral and byte-neutral.
3. **NOT FIXED: an unnameable mode gives undefined class answers.**
   `vx_offer` accepts X 640..1024 (multiple of 8) and Y 400..768, but the
   class byte names only four widths and four heights. For such a record
   `vx_encode` fails, and `vx_geom` writes whatever CL held to the kernel's
   `vx_class`. VXV_NEXT answers garbage, which can stick the Display page's
   walk or send it back to the first mode. The comments ("not offered:
   vx_offer refuses it first") say otherwise. The garbage differs between
   base and tip (different CL leftovers). It is the only base/tip difference
   the harness shows. The fix is in section 4.
4. Possibly worth a look (not proven): VX_SCROLL with `vgas_nb` = 0 ran
   65,536 iterations of its byte loop in the base. Through `vx_rowcopy` it
   moves nothing now. I did not check whether the kernel can send 0.

## NOT REACHED

Stopped at the coordinator's wrap-up. Queued and not done:

- the look-boot (QEMU `vesa`);
- `make small` was not run separately. None of my drivers is on kern_small
  (`$(SMALLDRIVERS)`), and the full `make` that builds `small360.img` is
  green;
- a second look at USBMOUSE's `um_wait`/`um_enum` layout: under about 4
  bytes priced, unproven.
