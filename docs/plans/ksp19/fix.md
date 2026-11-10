# Kernel size pass 19 - `fix`: the final soak's failures, triaged

Branch `ksp19-fix`, cut from `kernel-size-19` at `5b413f66`. Each failing row
is a PRODUCT bug this pass made (fixed in the code), a STALE ROW that encoded
a pre-pass contract (fixed to the new contract, keeping what it proves), or
PRE-EXISTING at `4beca096` (checked against a base build in
`/home/user/wt/fixbase`).

## SHARED CAUSES - read these first (for `fix2` too)

1. **The harness read W_SEG at 22.** R14 moved `W_SEG` 22 -> 18
   (`os88geom.W_SEG`, checked against `kernel/wm.inc`). Thirty-eight rows read
   a bare `rec[22]` / `u16(rec, 22)` - the next word - so a package's segment
   came back 0 or another's. Commit `b1608e34` points every one at
   `os88geom.W_SEG` / `dispcp.W_SEG`: telzm, telansi, pkgmove (all its
   rows), the browser rows (br*), lz*, telnet, telpen, ftpd, ftpdpix, drvcall,
   socktest, **dispcalc, dispcalcx, tmselfsu**, tymove, arcface, fontview,
   ethernet, pciether, thewire, wirechat, apple2part, c64part. **fix2's
   dispcalc and tmselfsu read the record this way** - re-run them on this
   commit before looking further.
2. **R14's conversion dropped handlers and the CF they cleared.** The class
   commit removed `OSAPI_WM_ON*` setter calls; two things went with them:
   * test packages whose class never got the handler (megaflt's `fl_tick`,
     megaown's `mo_tick`/`mo_wake`) - no timer ever fired;
   * **an entry proc's CF**: CLEAR SKIES' `cs_entry` ended with
     `cs_set_load`, whose failed read (no settings file) is CF = 1; the six
     setters after it had cleared CF. Every first launch was `LD_EABORT`.
     Fixed with a `clc` (`155b23bb`). **fix2's skiessound/skiesvga ("the
     window never opens") are very likely this** - re-run on this commit.
     An audit of every entry whose exit followed removed setters found no
     other (the rest end in `clc`, a flag-preserving cell, or wm_create's CF).
3. **T5's door in host models.** Unicorn rows that answered a rare cell by
   its table offset (`cells["OSAPI_..."]`) now get no cell: `tests/pxpartemu.py`
   models `int OS88_RARE` by index (`PxPart.doors`), MEM_FREE/MEM_MOVABLE as
   the no-op `retf`s they were.
4. **kbpara's KB->paragraph shifts take CL.** `mov cl, 6 / shl ax, cl` before
   `OSAPI_MEM_P` clobbers CL where the old KB door kept every register. In
   PiXEL's `px_claims` CL was the SCALE `px_wkp` reads next: every indexed
   1/1 decode ran the quantiser (zeros) and no GIF animated. Fixed `dd1e6e5c`
   (push/pop cx, +2 bytes). Worth checking at any converted site whose caller
   reads CX/BX after the claim.

## Per row

| row | verdict | fix | bytes | re-run |
|---|---|---|---|---|
| dosseam | stale row | `tests/dosseam.txt`: OSAPI_MEM_P (MEMP_CLAIM behind DHK_CLAIM) replaces OSAPI_MEM_CLAIM | 0 | ok |
| sbirq5, picomem sb leg | PRE-EXISTING at 4beca096 | `tests/picomem/sbhx.asm` includes `snddiag.inc` (sb.inc's slice calls SDG_SET since SPEC.md 127) | 0 | ok |
| pxanimemu (+pxextraemu, pxsavehost) | stale row | door model in pxpartemu (cause 3) | 0 | ok |
| megaflt | stale row (test package) | ONTIMER in fl_cls (cause 2) | 0 | ok, 6 closed |
| megaown | stale row (test package) | ONTIMER/ONWAKE in mo_cls | 0 | ok |
| megarmbref | stale row | rmk_3/rmk_6 gone with T5; FILE_RENAME's offset off the SDK; the door's bound read inside N (RD_P+RD_RK) and after (RD_N) | 0 | ok |
| megarehome | PRODUCT (every kernel) | CLEAR SKIES' entry `clc` (cause 2) | skies +1 | ok |
| redlinehost | workload changed | REDLINE's reference recalibrated on MartyPC, three adapters (MartyPC-only reference by its own record) | data | ok |
| netinst | PRODUCT | INSTALL's probe of OSAPI_ARG_TEXT/ARG_FILE at table offsets 0x04DD/0x0249 removed: both are doors now (it always took the plain start) and PKG_FMT 13 keeps older kernels out; SPEC.md 117.10.6 | install -~14 | ok |
| telzm | stale row | cause 1 | 0 | ok |
| pkgmove-dotdel/-wireflet/-browser | stale row | cause 1 | 0 | ok |
| pxdecode, pkgmove-pixelanim/-pixelgif | PRODUCT (every kernel) | cause 4 | pixel +2 | C1.PCX, all TIFs ok; pkgmove ok |
| abicompat | stale row (a new generation) | see below | 0 | ok (and both red arms red) |
| ftpd | stale row | cause 1; my own sweep had caught the BPB's byte 22 in its host reader, put back (twice: the fix2 merge carried it again) | 0 | ok (one run stalled at 8d's STOR-after-LIST, the next passed) |
| dispfit, colorpresets | stale row | dispfit's size-changed handler goes in a COPY of fm_cls (WC_ONRESIZE) in a spare record; colorpresets reads WC_ONCLICK/ONKEY via W_SEG:W_CLASS and OSAPI_THM_SET off the SDK (0x0440 was the old cell) | 0 | ok |
| doswpart | stale row | the DOS part's claim is the image at MEM_GRAN (paragraphs), not a KB count | 0 | ok |
| vgacolor | stale row | Force Color is bit 7 of 'VM''s mode byte since F1, not a 'VC' record | 0 | ok |
| paccman | stale row | cc_tpl is the 12-byte template, class word written by os88_wm_create | 0 | ok |
| doshead | PRODUCT | dos_htpl was still the 8-word template: its 6th word (dos_hpaint) read as the class, dos_wake never reached; `dos_hcls` | dos +10 | ok |
| excitebikeega | PRODUCT | xb_partget keeps BX (the head slack) across the MEMP_CONV claim - cause 4's BX twin | excitebike +2 | ok |
| stikswap | PRODUCT | OSAPI_DESK_ITEM's package remove enters sc_modcall with AL = 0xFF: C10's form left AL = 0 ("removed") when DESK.DRV could not be read, so a declined close went through | DESK.DRV +1 | ok |
| skies* (40 rows) | PRODUCT | cause 2 (CLEAR SKIES' entry CF) | skies +1 | `skies` ok; the rest in the re-run |
| telnet, socktest, lz*, br*, pciether, ethernet, thewire, wirechat, apple2part, c64part, fontview, tmselfsu, dispcalc, pkgmove-* | stale rows | cause 1 | 0 | in the re-run |

**Template audit.** Every `mov si, <tpl>` before an `OSAPI_WM_CREATE` in
apps/, drivers/, tests/ and kernel/ was checked for a sixth word that names
a class: dos_htpl was the only one left. **Register audit** of the KB ->
paragraph sites (BX taken by the verb in BL, CL by the shift): straight-line
reads after the call are all clean; the two found (PiXEL's CL, ExciteBike's
BX) were both read ACROSS an intervening call, which the scan cannot follow -
a reviewer of any other converted site should look for that shape.

**REDLINE, the numbers.** Against the OLD reference (calibrated at #252) the
graphics rows moved -17%..+16% - but REDLINE profiled on the BASE build
(4beca096, CGA) matches this tip's to within one count on every row but
rl_wire (-0.15%): this pass changed no workload's time. The drift is every
kernel since #252, folded into the new reference.

**abicompat.** The release (20261109) is PKG_FMT 10, the tip 13. Leg B
already says DECLARED and leg D shows all 61 packages and 17 drivers refused
at the version byte. Legs A and C now count moved cells / changed constants as
DECLARED when their file's guard moved (they cannot be reached), and leg F
holds the tip to REFUSING (LD_EVER) every release package that opens on the
release's kernel; its red arm stamps one with the tip's PKG_FMT. Leg A's red
arm judges strictly (no bump), so it still goes red.
