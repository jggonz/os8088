# ksp10 agent "hw" - fsx, vidsel, viddet, xmem, hiber/hb*, clone, compress, extmod, vmmouse

Appended as decided. Bytes are `kernsize`'s section deltas at each batch.

## TAKEN

### Batch 1

| where | what | big | small |
|---|---|---:|---:|
| fsx_run | release loop `mov cx, INST_MAX` -> `mov cl` (CH = 0: the door refuses CX >= 8, the fence clobbers DX/DI only) | -1 text | -1 |
| fsx_mode .modex | four `mov dx, 3xxh` -> `mov dl` (DH stays 3) | -4 text | 0 (GFX_VGA) |
| fsx_mode .fill | `mov al, ch / mov ah, FSXR_SIZE / mul ah` -> `mov al, FSXR_SIZE / mul ch` | -2 | -2 |
| fsx_mode .f1 | TEXT80 on Hercules: B800h -> B000h is one BYTE store of the high byte | -1 | -1 |
| fsx_wait/fsx_insync | the retrace timeout moves INTO fsx_insync (ZF = timed out, CF = not in retrace); both phases are `call/jcc`, insync loses its push/pop AX | -9 | -9 |
| fsx_page | Mode X start address: 19,200 = 4B00h, so the high byte is `page * 4Bh` (8-bit mul) and the low is always 0 | -6 | -6 |
| fsx_page | `mov dx, 3B8h` -> `mov dl`; both arms `jmp short fsx_wait.vsync` (the `.sync` near-jump trampoline is gone, the wait shrank into reach) | -2 | -2 |
| vid_ac_pal | two `mov dx` -> `mov dl` | -2 | 0 |
| vid_span_one | `cmp ax,dx / jbe .no` -> `cmp dx,ax / cmc`: the fit still falls through, now 2 clocks not 4 | -1 | 0 |
| vid_disp_planes | `mov cx,1 / cmp / jne / mov cl,4` | -1 | 0 |
| vid_setmode | Hercules arm `mov dx, 3B8h` x2 -> `mov dl`; mode 12h arm VGA_SEQ -> VGA_GC is `mov dl` | -3 | -2 |
| vid_text | `mov dx, 3BFh` -> `mov dl` | -1 | -1 |
| vid_apply | the six display-extent stores are one two-pass axis loop (lodsw/stosw) | -10 | -10 |
| hb_ok_x | the string chosen directly (`mov bx` writes no flag), no reason round-trip through AL | -10 cold | n/a |
| xm_free | its `stc/ret` refusal is xm_alloc's (`.stc` one byte below xm_alloc's `xor ax,ax`) | -2 | n/a |
| xm_copy | ES*16 as one `mul` instead of two shift pairs (286+ only reaches it) | -5 | n/a |

Batch 1 total: kern_big .text -50, .cold -10 (sum -60); kern_small .text -35.

### Batch 2

| where | what | big | small | emu |
|---|---|---:|---:|---:|
| vid_desk_union | banks BX too and leaves through `kret_di` | -2 | n/a | -2 |
| gfx_ink | `mov bx, gfx_inktab / xlatb`: 11 bytes -> 9, AND faster (book: 73 -> 66 clocks, fetch floor ~48 -> ~39) and AH preserved again. PER GLYPH CELL on 1bpp | -2 | -2 | -2 |
| vmm_poll / vmm_boot_x | the eight-register exits are `kret_es` / `kretfc_es` (kern_emu only; a 386) | 0 | 0 | -12 |

Running total at batch 2: **kern_big .text -54, .cold -10 (sum -64); kern_small .text -37;
kern_emu .text -60, .cold -16 (sum -76)**.

### Batch 3

| where | what | big | small |
|---|---|---:|---:|
| vid_6845_prog | one `out dx, ax` per CRTC register (index AL, value AH): 20 bytes -> 15. Two byte cycles, low first, on the 8088's bus and on an AT's 8-bit slot alike - the card sees the same index/data pairs | -5 | -5 |

Running total at batch 3: **kern_big .text -59, .cold -10 (sum -69); kern_small .text -42
(sum -42)**; kern_emu as batch 2 plus -5.

### Batch 4

| where | what | big | small |
|---|---|---:|---:|
| vid_mono_text | 3B8h after vid_6845_prog is `mov dl` | -1 | 0 |
| fsx_wait .vsync | 3BAh after 3DAh is `mov dl` | -1 | -1 |

Running total at batch 4: **kern_big .text -61, .cold -10 (sum -71); kern_small .text -43
(sum -43)**.

### At the tip

    kernsize[big]:   text 44,247 -61  bss 5,133 +0  cold 37,911 -10  lowbss 5,598 +0  vgabuf 336 +0  ovl 2,470 +0  ovlw 5,011 +0   (sum -71)
    kernsize[small]: text 32,861 -43  bss 3,104 +0  cold 23,950 +0  lowbss 2,868 +0  vgabuf 0 +0    ovl 2,086 +0  ovlw 1,480 +0   (sum -43)
    kernsize[emu]:   text 44,506 -67  bss 5,133 +0  cold 38,029 -16  lowbss 5,598 +0  vgabuf 336 +0  ovl 2,476 +0  ovlw 5,011 +0   (sum -83)

Per file, kern_big code (`kernsize --modules`): fsx 961 -> 935 (-26), viddet 791 -> 770
(-21), hiber 360 -> 350 (-10), vidsel 878 -> 871 (-7), xmem 242 -> 235 (-7). No `.bss`,
`.lowbss` or `.vgabuf` byte moved.

Run: `make` (fast tier 61/61 each batch), `make small`, `make emu`, the KBDDIAG=1 knob
in a private tree (`tools/os88build.py build KBDDIAG=1`), stkbalance over
kernel.asm + kernel/*.inc identical at base and tip; soak `dispfsxherc`, `hibernate`,
`pixelstein-vga` (Mode X + OSAPI_FSX_PAGE + the retrace wait), `xmcheck` (QEMU) - all
green; full-tier `bootsmoke` (both 1bpp adapters, after the 6845 change) green.

Modules (HIBER.DRV, CLONE.DRV/compress, EXTD.DRV) were NOT worked: their claims are
whole KB and none is near a step that bytes here could buy (hiber 5,489 bytes of
image, clone 9,211 - five under 9KB, i.e. headroom only - extd 1,023).

docs/INDEX.md is regenerated in this branch only because this notes file is tracked
(os88index lists every docs/plans/*.md); the coordinator regenerates it at the merge.

## REFUSED

* **fsx_mine's range test** (~6 bytes): deliberately kept, its own header says why
  (a sentinel landing in [sch_cur] would grant a bracket to nobody).
* **vid_pop8 -> kret_es** (~6): refused by its own header - vid_setmode/vid_text run
  from the splash before the ladder at the end of `.text` is resident.
  **vid_mono_text -> kret_es** (-2) is refused for the same reason: vid_text calls it.
* (vid_6845_prog as word OUTs was refused at -2 in batch 1 and TAKEN in batch 3
  at -5, once the loop was rewritten around it - see batch 3.)
* **vid_apply: fold the m1/m8 copy into the axis loop** (-2 at best, +1 once the
  second copy's DI has to be reloaded): wm8 has no y twin (writing one is vid_pw).
* **vid_disp_init's .jout trampoline**: every reordering measured is the same 26 bytes.
* **hbf_paint/onclick/kinit thunks** sharing a body: they differ in a table index,
  and a register to carry it costs what it saves.
* **clo_fnbuf to .bss**: same resident bytes either way.
* **vid_ctx_act: `push di`/`pop di` round the copy instead of `sub si,32 / mov di,si`**
  (-3): +~21 clocks on the display-crossing path under every drawing primitive of an
  extended desktop. Hot enough to refuse.
* **fsx_mode/fsx_run `.fail` at the foot** (CLC_OR_STC + one exit, -5 each): every
  refusal jump is >127 bytes from the foot, which is why the trampolines exist.
* **fsx_mode's `cmp al, FSXM_COUNT`** looks redundant with the caps shift (an id >= 9
  shifts the mask to 0) - it is NOT on a 186+: the shift count is masked to 5 bits, so
  id 32 would read as id 0. Kept.
* **vid_cga_equip as an entry into vid_equip's store tail** (-1). Not worth it.
* **hbm_kinit's work moved into hbm_open after the launch** (would delete hb_kinit's
  .text thunk and hbf_kinit, 16 resident): KD_INIT runs BEFORE the window is visible
  and sets the snap and the mouse-up hook; after the launch it is a behaviour change.
* **vid_tab's EGA row as the VGA row + a height patch** (18 bytes of row against ~19
  of code).

## CROSS-FILE

* **KD_INIT's .text thunks** (`fm_kinit`, `app_tmr_kinit`, `app_bounce_kinit` in
  kernel.asm, `hb_kinit` in hiber.inc - 6 bytes each, `call COLD_SEG:x_x / ret`):
  every one exists because inst_launch (instance.inc ~1570) does `call ax` near in
  KERNEL_SEG, while every OTHER callback of a kernel window is dispatched into
  `.cold` through `COLD_SEG:wm_cbd` (wm_pkgcall's `.near`). Dispatching KD_INIT that
  way costs +6 at the site (BP is the instance record there, so it is
  `push bp / xchg ax, bp / call COLD_SEG:wm_cbd / pop bp` beside the existing SI
  bank), the four cold bodies end in a near `ret` instead (`kretc_*` for
  `kretfc_*`, check each has no other far caller), and `cp_kinit` - a `.text` body
  in ctrl.inc - needs a cold entry of its own (~+6). ESTIMATE: **~-12 kern_big,
  ~+6 kern_small** (only fm_kinit is a thunk there). Probably NOT worth taking;
  recorded so nobody re-derives it.
* **`cw_mem_disp` (kernel.asm) IS `spw_near` (viddet.inc)**: both are `call bp /
  retf` in `.text`. `cw_mem_disp equ spw_near` and the three bytes go, both kernels
  (-3 / -3). spw_near must stay where it is (inside SPL_RESIDENT, the splash calls
  it); cw_mem_disp's callers only need KERNEL_SEG:a `call bp/retf`. ASSEMBLED in a
  scratch copy: exactly -3 on both kernels. Check os88ovlchk / tests/ovlrefs.txt for
  either name before taking it.
