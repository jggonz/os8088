# Kernel size pass 19 - `class`: R14 + K17-2, the window class in the package

Branch `ksp19-class`, cut from `kernel-size-19` at `174b80bf`. SPEC.md 11.104
is the contract; this is the record.

## TAKEN

### R14 - the window class lives in the package (+ BR-28's re-test)

* **Record.** The 13 handler words (10 on kern_small) are one `W_CLASS`
  (offset 12): a near pointer, in the window's own segment, to
  `{length word, style word, handler offsets in WC_* order}`. `WIN_SIZE`
  74 -> 50 (kern_big/emu/mega), 65 -> 47 (kern_small). Published layout:
  W_FLAGS..W_TITLE as before, W_CLASS 12, W_MENUS 14, W_DISP 16, W_SEG 18,
  W_TIMER 20 (kern_big).
* **Template** `{x,y,w,h,title,class}` - 6 words, `WT_SIZE` 12.
* **Slot order** (ABI): PAINT, ONKEY, ONCLICK, ABOUT, ONWAKE, ONCLOSE,
  ONMOUSEUP, ONRESIZE, ONRCLICK, ONSIZE, then kern_big's ONTIMER, ONDRAG,
  ONWHEEL. Length is a WORD (lets `wm_hnd` compare it with `cmp bp, [bx]`).
* **`wm_hnd`** (SI = window, BP = slot -> BP = handler / 0, ZF): the one
  reader, 35 bytes. Tests W_FLAGS bit 0 first - BR-28's liveness re-test in
  the one helper; the timer pass's and the wake's own copies dropped.
  Kernel windows' classes are `.text` tables (`app_cls_about`, `fm_cls`,
  `cp_cls`, `hb_cls`, kern_small's `fdlg_cls`), read through DS.
* **Style word** = the W_FLAGS the window is born with (`inc` for bit 0),
  applied before `wm_fit` so KEEPH holds at the first fit. `KD_WFLAG` gone
  (KD_SIZE 14 -> 13; the Disk window's SIZABLE is `fm_cls`'s style).
* **kern_small's answer**: `wm_create` returns AL = first undispatched slot
  (`WC_NDISP_BIG` 30 / `WC_ONTIMER` 24), AH kept. Browser, frotz, notepad,
  scribe, word, texpad, taskmgr, 1942 read it where they read
  OSAPI_WM_ONDRAG's/ONTIMER's CF before.
* **Runtime handler changes** are writes to the package's own class (no
  door): Paint's LIVE `WC_ONSIZE`/`WC_ABOUT`; kern_small's chooser points
  `W_CLASS` at `fdlg_cls`. The close-request pass, which zeroed W_ONCL, calls
  `app_close_now` (app_close_win without the ask).
* Withdrawn: 10 handler setters, `OSAPI_ABOUT_SET`, 6 flag setters (97 bytes
  of table), kern_mega's six `MEGA_ICWB` stubs and the `MEGA_ICWB(W)` shape,
  kern_mega's `wm_onmouseup/ondrag/ontimer/onclose` bodies and shims,
  `wm_sizable/snap/saveu/ownbg/keeph/noanim/quiet/about_set`.
  SDK table renumbered by script from the decoded base table (no hand
  numbers); `slot 0x...` prose re-pointed the same way.

### K17-2 - one `OSAPI_WM_FLAG` (took OSAPI_WM_SIZABLE's cell)

BX = window, AH = W_FLAGS bit NUMBER (`WFB_*`), AL = 0 clear / else set;
registers and flags kept. 46 bytes (branch-free set/clear-by-xor). Edges:
SAVEU clear drops the cache; NOSNAP clear snaps; KEEPH either way re-fits.
**WF_1BPP is now a bit of its own** (no longer cleared with SAVEU) - a claim
about colours; telnet states it with each promise, the rest are born with it.
No `wm_isrec` fence (packages are trusted).

### Measured (`kernsize --json`, against BRIEF 0.2)

| kernel | `.text` | `.bss` | `.cold` | resident | delta |
|---|---:|---:|---:|---:|---:|
| kern_big | 39,882 (-94) | 5,090 (-288) | 37,797 (-24) | 90,493 | **-406** |
| kern_small | 29,594 (-86) | 2,840 (-108) | 21,037 (0) | 56,339 | **-194** |
| kern_emu | 40,128 (-94) | 5,090 (-288) | 37,797 (-24) | 90,739 | **-406** |
| kern_mega | 45,542 (-186) | 5,128 (-288) | 48,559 (-24) | 129,869 | **-498** (conventional -474) |

Includes two never-executed PARITY bytes (after the table, all kernels; and
kern_big's longer `app_close_win`) for t_hotpar. Spans: `wm_hnd` 35,
`wm_flag` 46.

**Packages** (77 asm packages/drivers built in both trees): unpacked images
1,620,939 -> 1,619,258 (**-1,681**), on disk 1,899,681 -> 1,897,836
(**-1,845**). C packages, unpacked: cword -100, runcpm -80, c64 -122,
apple2 -100, paccman -88, lemmings -128, weave -172, loom -126 (**-916**).
A few small ones grow by a class whose last named slot is a late one
(ether/pciether +8, the three Sticky Apps +16 each - their five stik slots
were the DESK.DRV library's installs, which shrank - tank +16); the row is
net negative everywhere it is measured in aggregate.

**Cycles - over the predicted band, said plainly.** `wm_hnd` by count (8088,
EU timings): call + body + ret ~238 clocks against the ~24 of
`mov bp,[si+W_ONx]` / `or bp,bp` it replaced: **~+214 clocks (~45 us) per
callback** dispatch, vs flagday's predicted +40..+90 (+50 for a called
helper). It is per callback, never per pixel/glyph. Cheaper forms recorded
below; not taken because each grows bytes or needs a record word.

### C SDK (owner answer 3)

`os88.h`: `struct os88_class` (always the full 30 bytes), `OS88_WF_*`,
`OS88_WFB_*`, `os88_t_*` trampoline names (crt0 aliases, gated on
CC_HAS_*), `os88_wm_create(..., cls)`, `os88_wm_flag()`. Removed:
`os88_about_set`, `os88_wm_on{mouseup,resize,timer,close,wake,wheel}`,
`os88_wm_{sizable,snap,keeph,ownbg,saveu}`. All 8 C packages + ccsmoke
declare a static class; the six hosttest mock headers/uitests follow
(a2uitest now asserts on the class).

## REFUSED / RECORDED

* **Faster dispatch** (would bring R14 into its band): (a) fold the lookup
  into `wm_pkgcall` after its DS switch and answer "no handler" in BP,
  keeping `wm_hnd` only for the pre-tests (~+90 per dispatch; needs every
  site's pre-test reworked); (b) a `W_CSEG` record word so the class is one
  `lds` (+24 .bss kern_big). Neither taken in a size pass.
* `STKL_D_CLICK/MUP/TICK/CLOSE/WAKE` (Sticky App descriptor words) are now
  unread - 10 bytes per Sticky App and the library's copies. Not removed
  (descriptor ABI, DESK.DRV); a clear wave-C size win.
* ~140 comment mentions of withdrawn cells remain in package prose (history,
  harmless); SPEC.md 13.x/75.1 headings re-pointed, bodies not rewritten.

## WANTS THE OWNER

* Accept ~+214 clocks/callback for R14 (predicted +40..+140), or ask for (a).

## Next wave must know

* The table lost 97 bytes; t5 renumbers anyway. A one-byte PARITY pad sits
  right after `osapi_table_end`; drop or move it when t5's own delta is known.
* `WC_*`, `WT_CLASS`, `OS88_CLASS` and `WFB_*` are SDK names t5 must keep.
* `tools/os88geom.py` mirrors WIN_SIZE 50/47, W_MENUS 14, W_SEG 18.

## ROWS (the final pass's union)

fast (ran green in `make`): api-abi abilock mirror stik hotpar stkclass
btnrules. Emulator rows this reaches: timerrace (rewritten: W_TIMER 20),
desklayer (reads the class's WC_ONKEY now), mouseup, muptest, btngesture
btnall btnmove btncp dragui, alertbtn alertanim, wheel mouwheel wheelps2,
dispclose dispclose-small dispapp dispapps dispcalc dispnp dispsave
dispzoom dispprefer, dmgcull, su1bpp win1bpp tpsaveu tmselfsu tmdmg,
sizesnap, fdlgchoose fdlgchsmall fdlgup fdlgsmall, cpup cppromise cplistrow,
hibernate hibernatedrv, stikboot stikswap stikmenu stiknodesk stiknote
stiknotesmall stiksmall deskclock deskcalperf deskhover deskclose deskmodal,
paintsize paintnogrow paintnogrow-vga paintrz, sheetdlgclose sheetbtn
sheetask, calcflick brclick ftpd ftpdfocus telnet trkclick wdclick wddrag,
weavesmoke weavesession weavegame, lemmings paccman c64part apple2part,
win16np win16mines win16desk, megapkg megaflt megahigh megaown megapaint,
rdgclose gbwindow isotronwin pixelstein-win pxswin dosbar doswleave
doscon, installabi abicompat, netinst thewire megacdrom.

Looked (one MartyPC boot each, `os8088_5150_cga` and `os8088_xt_vga`):
Calc, Note Pad, Paint open with their class styles (SAVEU shown on the
record), Paint's runtime-written About opens its card, grow box present.
