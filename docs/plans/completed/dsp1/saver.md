# Driver size pass 1: saver (SAVER.DRV, THMPAL.DRV)

Agent **saver**, branch `dsp1-saver`, owning `drivers/saver/` and
`drivers/thmpal/`. Base `de93581a`. The pass stopped at the coordinator's
wrap-up. Two items were queued and not started, and they are listed under
NOT REACHED.

## 1. BASE AND TIP

| driver | image base -> tip | bss base -> tip | file base -> tip | 360KB system disk clusters |
|---|---:|---:|---:|---:|
| SAVER.DRV | 10,247 -> **8,933** (-1,314) | 0 -> 224 | 7,108 -> **6,009** (-1,099) | 14 -> **12** (-2) |
| THMPAL.DRV | 2,575 -> **2,402** (-173) | 0 -> 0 | 2,259 -> **2,133** (-126) | 5 -> 5 |
| **total** | **-1,487** | | **-1,225** | **-2** |

`os88diskuse --diff` against the base shows `os8088-360.img` going from 37 to
39 free clusters, with no other image changed. Both drivers ship on every
system-disk geometry and on kern_mega's disks, so the file bytes are saved on
each of them. SAVER's claim is image + bss rounded to 512, and it goes from
10,752 to 9,216. That is a consequence of the bytes, not the reason for them.
SAVER's "bss" of 224 bytes is the os88drv strip reaching the lower rung: the
mode union was always trailing zeros, and the strip stops at the claim rung.

`thmrows.inc` is untouched, and `build/kernel.bin` was `cmp`-identical to the
base after the final build.

## 2. TAKEN

**How equivalence was shown.** The proof is a unicorn harness in my scratch
(`harness.py` for SAVER, `thmh.py` for THMPAL). It loads the real image, runs
it through its own +12 dispatcher, and stubs every API cell and the rare door.
It logs each drawing call semantically: fill rects and pen, BLIT1 geometry and
an md5 of the band, every POINTS list, FONT_RUN strings, and the window calls.
It also logs the kernel-side bytes the image writes, which are the settings
block, and THMPAL's row, 'TP' and cstrp.

* **SAVER** ran every mode alone, all four, and two pairs, on VGA, EGA, CGA
  and Hercules, plus a 300x100 screen that must refuse. Each run was two
  sessions on one loaded image with forced lag re-anchors, plus seconds 0 and
  255. The settings window was driven through every check box, both fields,
  the gaps, every button, slide-offs, keys, Tab, Return, Escape, Test (a
  session under the open window), reopen and close.
* **THMPAL** ran kind x style bits x preset (an unknown one included), with
  THM_SET succeeding and refusing.
* **Both** ran with two different post-call flag values.

The digests were identical base against tip: SAVER at 300 and at 1,500 frames
a scenario (1,174,629 log entries), the NOHEDGE=1 build, and THMPAL in all
four runs. The harness is red when it should be: with the sine mirror's `neg`
broken, the digest changes.

The CF a callback returns is compared only for W_ONCLOSE. `kernel/ui.inc` and
`wm.inc` read it there and nowhere else. A first run differed only in an
ignored CF and in don't-care registers of WM_SHOW and WM_DESTROY, which read
BX alone.

### SAVER, cold items

| item | image | file |
|---|---:|---:|
| `OS88LINE_NORESYNC`: os88line_resync was never called | -43 | ~-40 |
| **Sine table: only entries 0..64 ship.** `sv_do_start` mirrors the other 191 (24 bytes of code, about 4,500 cycles once a session). The table is still `wiresin.inc`: it is read through a redefined `db` that emits the quarter and records the rest, and the build fails if `wiresin.inc` ever stops being the symmetry the loop rebuilds. | +24 | ~-165 |
| sv_disp ordered by verb number, and sv_do_busy is `neg` | -9 | |
| sv_lone, sv_secs, sv_clear, sv_enabled and sv_step written into their one caller. The `sv_step:` label is kept, alone on its line, for fishfit and fishedge. | ~-60 | |
| sv_pick's walk: `inc ax / and al,3` wrap, modes read once | ~-15 | |
| sv_rndxy, one helper for the cube's and the figure's random centre | ~-45 | |
| sv_rsin/sv_rcos fall into sv_mulsh, which shares sv_sh7's tail through a `db 3Dh` swallow. sv_cos falls into sv_sin. | ~-35 | |
| sv_rotset as one store pair called twice | -10 | |
| Shapes: sv_shape_init/step unrolled for SV_HNF = 2 (asserted). [sv_hf] is a word. hspawn tail. hverts in registers (BP vertex counter, CL theta, CH spike, byte divides that are proved under 256). hput, hedges, hvi, hinitw reading the walk's own DX/DY back. -12 bytes of union. | ~-330 | |
| Stars: init and born, with `cwd` abs and the bound loaded once | ~-25 | |
| Fish: born with the edge written in, off/size through sv_fsz, sv_fun with `cmc`, bubbles banked on the stack | ~-100 | |
| svcfg: num2str+seed are sc_fld (the zero case was redundant). Paint rows. sc_btns with btn1 inlined and a static rect pointer. sc_bhit with `lodsw`. Click with no sc_cx/sc_cy. sc_focus with its fields tail. Key, store/apply/test, kill, str2num. | ~-240 | |

### SAVER, hot items

Every per-frame path is faster. Counts are 8088 clocks.

* **Dispatcher.** The frame verb falls through instead of `jne +3 / jmp near`.
  That is -15 on every poll, hundreds a second. The deadline's two arms share
  one add: -13 on the due path, and the re-anchor path is equal.
* **Cube.** The four parts are one routine, with 4 call/ret pairs gone (-156).
  The vertex walk drops its push/pop of SI and CX, which rot3 keeps
  (-54 x 8). Vertices project straight into mask coordinates, which takes 2
  `sub mem` out of each of the 24 mpt calls (-912). sv_cml keeps its whole
  walk in registers: the bounds are one OR test, and the error terms and row
  step are no longer read from memory per pixel (about -16 to -48 a pixel,
  about 10,000 a frame).
* **Stars.** The bank is one `rep movsw` (-2,100). Plot and proj are inline
  (-2,640 call overhead, -7,900 proj overhead). Bounds are one unsigned
  compare each. pts and emit lose their pushes. The drift loop adds about 40
  once in 4 frames and saves the 39 call every frame.
* **Shapes.** hrun and the retire/scan loops run in registers: -65 a retired
  edge, -36 a scanned edge, -80 at the front.
* **Fish.** fish_one, gone, where and bub_one are inline (-117 a swimmer).
  bnd_build and shift are inline (-39 a band, -39 a row). sv_fsz replaces two
  `mul`s (-125). `stosw` does the doubling, and the bnsh word saves a per-row
  `sub`.
* **sv_ink, sv_sin.** `xlat` saves about -50 and about -8 a call.
* **The swallow.** The `db 3Dh` costs 4 a mulsh. Every frame-path mulsh
  follows an sv_sin, which the `xlat` already made cheaper by more than that.

No transfer call was added. The settings window, init and spawn paths are
cold, and they are faster as well.

### THMPAL, all cold

* tv_entry and tv_install take os88_ent with the `es` ladder rung.
* Dispatch by verb number, and busy is `neg`.
* tv_paint uses tv_layout.
* The painters no longer bank what tv_paint banked.
* tv_list_paint walks row tops by addition, with no `mul` and no tv_rowy.
* tv_btns has btn1 inlined.
* tv_bhit is the sc_bhit shape, with BL kept so tv_armed is gone.
* tv_key's Escape falls into Return.
* tv_cancel loads opreset and ostripes as one word. Their adjacency is
  asserted.
* tv_stylebits_get is inlined.

The result is -173 image, -126 file. `OS88_RET_LADDER` rose from bp to es,
which is 1 byte.

## 3. REFUSED

* **`sv_ival` constant-folded** (about -22). Every mode sets it to 1, but
  `tests/saverate.py` reads `[sv_ival]` out of a listing. It is also a
  documented hook ("the mode's to raise").
* **sv_entry on os88_ent** (-7). The frame verb is per pass, and the ladder is
  cold-only (os88drv.inc).
* **Loop-folding sv_cube_move's two axes** (-18). It adds about 70 cycles a
  frame to that routine. The cube frame is far faster overall, but the move
  was not touched.
* **sv_hbase `neg`/`and` in place of three `mul`s.** It is +2 bytes, though
  about -700 cycles a frame.
* **Unrolling the sine mirror into the union, behind a guard** (about -14
  image against the inline loop). It puts code in bss that a later mode
  overwrites, which is too clever for 14 bytes.

## 4. WANTS THE OWNER

* **The geometry's spike latitude** (DEFECTS FOUND 1). Fixing it changes the
  picture.
* **SAVER's frame poll.** An early "not due" test ahead of sv_entry's seven
  pushes saves about 190 cycles a poll, hundreds of times a second, for about
  +8 bytes. It is speed, not size.

## 5. WANTS AN ABI BREAK

None.

## 6. CROSS-FILE

* **apps/os88ui.inc.** `os88ui_bhit` (23 bytes) is assembled in SAVER, where
  nothing calls it. It wants an `OS88UI_NOBHIT` knob beside
  `OS88UI_NOBFIND`. THMPAL does call it.
* **apps/os88gfx.inc.** `gfxe_wline` (17 bytes) comes with `GFXE_WALK` in
  SAVER, uncalled. It wants a knob.
* **apps/os88line.inc.** `je os88line_left/right/home/end` are four relaxed
  jumps: 12 bytes in every includer.
* **Makefile.** The `saver.bin` comment ("a sine table ... the kernel spends
  126 bytes") is now stale on the table.
* **Attach-only code.** Neither driver has an attach. The cold-at-runtime
  halves are SAVER's settings window (svcfg plus os88line plus os88ui:
  roughly 2,300 bytes of the 8,933, live only while the window is up), and
  THMPAL as a whole. Both are already freed with their owner's reap, so
  nothing is to be gained from a shrink verb.

## 7. ROWS TO RUN

* **SAVER:** `saver`, `savervga`, `fishfit`, `fishedge`, `saverate`,
  `dispsaver`, `wire` (t_wire pokes `[sv_hlim]`), `buildmatrix` (NOHEDGE),
  `mirror` and `stkapps`. The last two are fast tier and were green.
* **THMPAL:** `colorpresets`, `thm95` and `dispthm`.

## 8. LOOKED

No boot. The harness covered every path the changes touch, and the
coordinator's wrap-up ended the pass first.

## 9. DEFECTS FOUND

1. **svshape.inc, not fixed.** In `sv_hverts`, every other point of an
   `SV_HSP_IN` shape (Flow) is meant to be pulled IN. Its latitude is
   clobbered instead: `sv_mulsh` leaves AH equal to the result, so
   `[sv_hlph2]` received the pulled-in radius. Ring A's odd points sit at
   latitude 27 instead of 44, and ring B's at 47 instead of 0, so they are
   lifted out of the ring's plane. The tip reproduces this exactly. It was
   found by reading.
2. **Mirror trap, not fixed (it is not a defect today).** sv_hput's header
   said it clobbered AX, BX and DI. It also spent CX and DX (`mov cl,6`,
   `imul word`). The header now says so.

## NOT REACHED

* **sv_rot3's three blocks as a table-driven loop.** About -20, and it needs
  a cycle count first.
* **Fish band build in registers.** The mask and step are still memory bytes,
  and rows could combine expand and shift. About -20 to -30.
