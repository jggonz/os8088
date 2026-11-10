# Kernel size pass 19 - `wmrec`: the window record and `wm_hnd`'s dispatch

Branch `ksp19-wmrec`, cut from `kernel-size-19` at `7ba85e9f`. The question:
R14's `wm_hnd` costs ~+214 clocks a callback over the old record words; can
the record be laid out so `{W_CLASS, W_SEG}` is one far pointer (`lds`), and
the dispatch be made smaller AND faster?

## TAKEN

### The class pointer is a true far pointer for every window - without moving a field

The obstacle to the `lds` layout is in "REFUSED" below: the word under
`W_SEG` is already the offset half of the dispatch pointer `W_DISP`. What the
`lds` was for - one load of the class's segment with no test of which kind of
window it is - is had another way, at no record bytes:

* **A kernel window's `W_CLASS` is its class's offset from SEGMENT 0** on
  kern_big, kern_emu and kern_small (`WM_KCLS_BIAS` = `KERNEL_SEG` x 16). A
  kernel window's W_SEG is 0, so `{W_CLASS, W_SEG}` names the class exactly as
  a package window's does, and `wm_hnd` loads DS from W_SEG unconditionally:
  the `cmp word [si+W_SEG], 0` / `je` pair (6 bytes, 27-31 clocks) is gone.
  `wm_create` adds the bias to the HIGH BYTE (`add byte [bx+W_CLASS+1], 6`,
  4 bytes: the bias's low byte is 0, asserted) where it zeroes W_SEG;
  kern_small's chooser writes `fdlg_cls + WM_KCLS_BIAS`. kern_mega keeps the
  test and an unbiased pointer (segment 0 is the null selector there).
  `kernel.asm` fails the build if `.text` would end past 64KB of linear
  memory (`KTEXT_SIZE + WM_KCLS_BIAS > 0x10000`) - broken on purpose with a
  4KB limit and seen to fire. L1 (KERNEL_SEG 0x01A0) keeps it: bias 0x1A00,
  `.text` must stay under ~58KB, and the guard says so if not.
* **`wm_hnd` hands BX back as the window** (= SI) instead of banking the
  caller's BX: `mov bx, si` for `push bx`/`pop bx`, two bytes either way,
  19 clocks cheaper. Every caller had BX = SI already or BX dead (audited all
  14: ask_close, ask_size, wake, paint, WM_SZNOTE x3, menu About, key, release,
  content click, timer, drag edge, right click, the app-name item). That buys:
  `wm_wake_call`'s `mov bx, si` (-2) and `wm_draw_win`'s two `mov bx, di`
  after the paint lookup (-4; `wm_clip_set` keeps BX). `menu_ab_cell`, which
  zeroed BX for Close's index BEFORE the call, keeps it in DX instead (dead
  there, kent_bp banked it): +2, even - the `mov bx, 0` form is +1 and put
  `ui_task` at odd parity on both gated kernels.

### Measured (`kernsize --json`, this branch's base `7ba85e9f` -> tip)

| kernel | `.text` | resident base | resident tip | delta |
|---|---:|---:|---:|---:|
| kern_big | 39,775 -> 39,769 | 90,058 | 90,052 | **-6** |
| kern_small | 29,550 -> 29,544 | 56,240 | 56,234 | **-6** |
| kern_emu | 40,021 -> 40,015 | 90,304 | 90,298 | **-6** |
| kern_mega | 45,418 -> 45,414 | 129,000 | 128,996 | **-4** |

Spans: `wm_hnd` 35 -> 29 (35 on kern_mega); `wm_create` +4 (real-mode only);
`wm_wake_call` -2; `wm_draw_win` -4; `menu_ab_cell` +2. **Packages: 0** - no
SDK byte moved (`apps/os88api.inc`, `os88.h`, `tools/os88geom.py` untouched;
every `.o88` md5-identical to the base build; the kernel modules re-link
against the moved addresses). No abilock change.

### Cycles (8088; EU = execution-unit clocks, floor = each instruction at
max(clocks, 4 x bytes))

`wm_hnd` found-handler path, call through ret (the `mov bp, WC_x` and `jz`
at the site are the same before and after: 12 + 8 at the floor):

| | EU | floor |
|---|---:|---:|
| before R14 (`mov bp,[si+W_ONx]` / `or bp,bp`) | 24 | 37 |
| R14 (base), package window | 234 | 264 |
| R14 (base), kernel window (`je` taken, no `mov ds`) | 225 | 251 |
| **wmrec, either kind, real-mode kernels** | **182** | **214** |
| R14 (base), kern_mega | 234 | 264 |
| **wmrec, kern_mega** | **209** | **245** |

So R14's +214 (floor +227) a callback is now +158 (floor +177) on kern_big,
kern_emu and kern_small: **-52 EU / -50 floor clocks a callback** (~11 us at
4.77 MHz), -25/-19 on kern_mega. On top: a wake dispatch -2/-8 and a paint
dispatch -4/-8..-16 (the dropped moves). Instruction by instruction (EU):
call 23, push ds 14, test [si],1 16, jz 4, mov bx,[si+12] 21, mov ds,[si+18]
21, cmp bp,[bx] 18, jae 4, add 3, mov bp,[bx] 17, test-imm 4, pop ds 12,
mov bx,si 2, or 3, ret 20.

**BR-28's liveness re-test does not ride for free**: it is `test [si],1` /
`jz`, 5 bytes and 20 EU (24 floor) clocks, and stays. The only free form
found - a dead record whose `{W_CLASS, W_SEG}` names an empty class, so the
length test answers "none" - needs `wm_destroy` to write both words (+10) and
every never-used `.bss` record to be seeded at boot; +5 resident net. Not
taken.

## REFUSED (priced)

* **The `lds` record.** `lds bx, [si+W_CLASS]` needs the class pointer in the
  word under `W_SEG`, and that word is `W_DISP`'s offset - `call far
  [es:si+W_DISP]` (wm_pkgcall, sched.inc's rate hook) needs `{PKG_DISP, seg}`
  adjacent too. Two ways out, neither smaller-and-faster:
  (a) a second copy of the segment beside the class: +2 bytes a record
  (+24 `.bss` kern_big/emu/mega, +12 kern_small) plus its write and a
  MEM_RR_ROW relocation row - grows;
  (b) drop `W_DISP`'s offset (it is the constant PKG_DISP except HMA_DRV's
  drivers, DRV_HMA_ORG + PKG_DISP) and build the far call on the stack
  (`push cs` / `call tramp` / `push ds` / `push word [cs:k]` / `retf`): -2 a
  record (-24/-12 `.bss`) and -5 in wm_create, but ~+47 clocks a dispatch
  against `lds`'s -9 (33 vs 21+21; the bias above already removed the kind
  test `lds` would also have removed), and HMA_DRV needs its offset computed
  in the trampoline. Smaller, SLOWER: recorded for the owner, not taken.
  (c) the dispatcher in the class (`call far [W_CLASS]` landing on a
  `call bp / retf` at the class's head) removes `W_DISP` outright but costs
  every class 4 bytes of code (~+300 package bytes) - refused.
* **ES instead of DS in `wm_hnd`** (output ES = the window's segment): -22
  clocks, 0 bytes, but every caller's path after the lookup would have to be
  audited for ES = KERNEL_SEG; not worth 22 clocks unmeasured.
* **class.md's (a), the lookup folded into `wm_pkgcall`**: every site still
  needs its pre-test (no handler = no lock, no bill; the About item's
  existence), so the class is read twice; no saving found.

## WANTS THE OWNER

* (b) above: roughly -20 resident on kern_big/emu (-10 small; the trampoline and the HMA arm against -24/-12 of `.bss` and -5 of wm_create) for ~+47 clocks a
  callback dispatch, if the next pass wants bytes over clocks.

## ROWS (the final pass's union)

fast (green in `make`, kern_big and kern_small): hotpar (wm_paint_all and
ui_task held at their pinned parity; no pad added), api-abi, mirror, stik.
Emulator rows this reaches (every handler dispatch, kernel windows' class
through segment 0, kern_small's chooser class): class.md's list stands -
dispapp dispapps dispcalc dispnp dispclose dispclose-small, dmgcull,
timerrace, mouseup muptest, btngesture dragui, alertbtn, wheel mouwheel,
sizesnap, fdlgchoose fdlgchsmall fdlgup fdlgsmall, cpup cppromise,
hibernate, stikboot stikmenu stiknote stiksmall deskclock desklayer
deskclose, paintsize paintrz, megapkg megaown, installabi.

Looked (one MartyPC boot each, `os8088_5150_cga` and `os8088_xt_vga`, the
shipped 360KB pair): the Disk window (a kernel class through segment 0)
opened, listed and navigated B: and A:; Calc opened and closed from its
app-name item; Paint opened and its run-time-written About card showed;
Note Pad's File > Open chooser opened and cancelled; Sticky Note launched
from A:/STIKAPPS and drew its pad.
