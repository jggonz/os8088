# Kernel size pass 19 - `door`: T5's software-interrupt door, proven on the kernel's own traffic (P4)

Branch `ksp19-door`, cut from `kernel-size-19` at `174b80bf`. Rows: T5's door
(ABI-BREAK-LIST 13.2) built and run, P4 (the `cw_` shims behind it), abi-4's
`API_RN_DSOFF`, V7's reading. SPEC.md **20.3.4** is the contract this note
measures; SPEC.md 20.3 carries the IF rule, 53.2.3 the bracket's refusal,
96.12 DOS's half.

## BASE AND TIP

`tools/kernsize.py --json` on each kernel; base is the brief's 0.2 table.
Resident = `.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf` + `.vga`.

| kernel | `.text` | `.cold` | other sections | **resident** | spans |
|---|---:|---:|---|---:|---|
| kern_big | 39,976 -> 39,974 (**-2**) | 37,821 -> 37,745 (**-76**) | `.ovl` +14 (the vector install, boot-only), `.boot3` -4 | **90,899 -> 90,821 (-78)** | `rd_tab` 54, `os88_door` 51 |
| kern_small | 29,680 -> 29,699 (**+19**) | 21,037 -> 20,991 (**-46**) | - | **56,533 -> 56,506 (-27)** | `rd_tab` 32 (16 entries), door 51 |
| kern_emu | 40,222 -> 40,220 (-2) | 37,821 -> 37,745 (-76) | as kern_big | **91,145 -> 91,067 (-78)** | as kern_big |
| kern_mega | 45,728 -> 45,729 (**+1** conv) | 48,583 -> 48,507 (-76, above 1MB) | `.mega` 24,704 -> 24,720 (+16: the bracket's two bound writes) | conventional `MEGA_RESIDENT` 62,464 -> 62,464 (+0 rung; +1 byte) | door 54 (KDS is a selector load) |

`.bss`, `.lowbss`, `.vgabuf`, `.vga` unchanged everywhere; `KERN_SIZE`
unchanged on all four. `tests/megaseg.txt` 3,760 -> 3,763 (the door's
`mov ds, [cs:k_dsel]` against `push cs / pop ds`; the line says so).

**Packages and modules.** No package site moved (T5's). The kernel's own sites,
-2 each, landed in: `.cold` (files 15, desk 2, desksc 2, blank 3, menu 2,
mouse 2, memory 2, shutdown 1, loader 2, driver 3, sched 1 - the -76) and the
module images, PREDICTED from the site counts: CTRL.DRV -18 (9 sites),
DOCK.DRV -18 (9), DESK.DRV -8 (4), STIKMENU.DRV -6 (3), EXTD.DRV -4 (2),
HIBER.DRV -10 (5), and on kern_small FDLG.DRV -12 (6). **DOS.O88's core is 2
bytes SHORTER** (16,572 -> 16,570; kern_mega's 17,222 -> 17,220) with the 81h
virtualisation in it - see TAKEN 4. kern_dos's image: -2 (the same core).

## TAKEN

### 1. The IF audit (owner's answer 2) -> the door RESTORES IF

Every `OSAPI_*` and kernel `cw_` call site in `apps/`, `drivers/`, `tests/`,
`kernel/`, `kerndos/` and `dostools/` tagged by whether IF can be 0 there
(after a `cli` and before its `sti`/`popf`, or in a routine whose extent ends
in `iret`; a source scan, its false positive below):

| caller | cell | IF = 0 because |
|---|---|---|
| `apps/os88spk.inc` 307, 316, 376, 402, 901, 956 (and `tests/covbench`, `tests/megarmb/megarmbs` the same macro) | `OSAPI_FSX_SPK` (RSLOT) | SPEC.md 34.11.1: "IF = 0 FROM THE DOOR until IRQ0 is the ISR's" - deliberate |
| `apps/taskmgr/taskmgr.asm` 2406, 2409 | `OSAPI_MEM_COMPACT` (RCX), `OSAPI_MEM_P` | the one-moment claim snapshot - deliberate |
| `kernel/driver.inc` 4588 (`drv_task_x`) | `cw_task_spawn` (kernel-private, now a door index) | count-then-spawn in one IF = 0 window - deliberate |
| `drivers/sound/snddiag.inc` 364, `tests/megarmb` 239/255 | `OSAPI_CPU_INFO`, `OSAPI_GET_TICKS` | INLINE cells - never behind the door |

No ISR calls a cell (the one `isr` tag, os88spk.inc:402, is the `.pm` arm of a
routine whose extent the scan ran past into an ISR; it is a pushf/cli site),
and SPEC.md 53.2.2 forbids FSX rate hooks any kernel call. **Decision: the door
restores the caller's FLAGS (`push [bp+6] / popf`, 4 bytes against `sti`'s
1).** The lean door would open three deliberate critical sections. The rule is
in SPEC.md 20.3: *a rare cell may be called with interrupts off, and runs with
the caller's IF.* (Exempting the IF = 0 cells instead - keeping them as table
cells - costs 4 bytes a cell over a door word, so the restore is cheaper as
soon as there is one.)

### 2. The vector: 81h

`OS88_RARE equ 0x81` in `kernel/kernel.asm` and `apps/os88api.inc` (paired by
`t_mirror`). Nothing in the tree hooks or calls 81h-FFh (grep of every
`int`/IVT write; the one hit, memory.inc's note on C1h/C3h, is an XT-IDE ROM's
scratch words). 80h-85h is IBM's "reserved for BASIC" block, used by nothing;
86h-F0h is ROM BASIC's while it runs (a DOS program starting BASICA writes them
directly, and windowed DOS does not bank them); F1h-FFh are for programs; 80h
is the packet driver's last candidate (`dospktabi.inc`); 67h EMS; 70h-77h the
AT's slave PIC, which kern_mega leaves there (its master PIC is at 20h-27h in
protected mode). SPEC.md 20.3.4 has the table; `tools/os88intguide.py` carries
81h with three anchors (`--selfcheck` ok, 102 anchors).

### 3. The door (SPEC.md 20.3.4), every kernel

`os88_door` and `rd_tab` in `.text`, below the includes where the `cw_` shims
are. `RDENT RDK_*, routine` declares an entry (index `rix_<routine>`) and
refuses one out of kind order; kinds RS 0, RCS 1, RX 2, RCX 3, RN 4; only the
kinds with entries are assembled; 128 entries at most (the range compares are
on index x 2 in BL). `RD_N`, `RD_B1..RD_B4` are equates for decoders.

* **Installed** by `sched_init` (boot-only `.ovl`/`.boot3`: +12 there, 0
  resident) in the same IF = 0 window as IRQ0. Hibernation images carry the
  whole IVT (HS_IVT), so resume is covered; nothing else re-banks 81h (DOS's
  windowed exchange set is 00h, 08h-0Fh, 10h-3Fh; full-screen banks and
  restores the whole table).
* **kern_mega**: `mega_idt_build` makes every IVT entry whose segment is
  KERNEL_SEG an interrupt gate to the kernel's code selector, so gate 81h needs
  no code; the gate clears IF and the door's restore puts it back - the same
  contract, so no trap gate (it would save nothing). Package and `.cold` CS
  selectors are readable, so `lds` reads the index through them. **The
  real-mode FSX bracket**: `fsx_rdownf` writes 0 over `os88_door.lim`'s
  immediate, `fsx_rupf` writes `RD_N` back (+10 `.mega`, 0 conv), so every
  index refuses there (CF = 1, AX = 1, the index stepped over).
* **The refusal** (index >= RD_N): CF = 1, AX = 1, everything else kept,
  `retf 2`. 10 bytes, inline.
* **Tails**: the door's bodies end in `retf 2`. The OLD bodies (`api_rs`,
  `api_rsc`, `api_rx`'s head, `api_rxc`, `api_rn`) stay `retf` while six-byte
  cells use them - T5 deletes them when their last cell moves (74 bytes on
  kern_big, map spans 0x0511-0x0516, 0x0522-0x054E, 0x055F-0x0578); `api_x`
  stays for the hot XCELLs. The door's RX/RCX bodies have their own
  `retf 2` tail (`.xtail`), so nothing hot is touched.
* **abi-4**: `API_RN_DSOFF equ 10` beside `api_rn`; `dwsf_dskw_write_seq`
  reads `[bp+API_RN_DSOFF]`. The door's RN body builds the identical frame.

**Size, measured.** RS-only door (P4's) = 51 bytes on kern_big/small/emu
(54 on kern_mega). A scratch assembly with one entry of each kind
(osapi_vol_kind_x, mmf_osapi_mem_avail as RCS; wm_create RX;
mmf_osapi_mem_huge RCX; dwf_dskw_delete RN) = **132 bytes**, disassembled and
checked: bounds 0x20/0x36/0x3A/0x3C/0x3E for 27/2/1/1/1 entries, each body
the old one past its head. So T5's door costs **132 - 74 = +58** against the
sketch's +75..95.

**Cycles, PREDICTED** (8088 clocks, each instruction at max(its clocks, 4 a
byte fetched)):

| path | clocks around the routine |
|---|---:|
| far call to a `cw_` shim (`call far` 36, `call` 23, `retf` 34) | 93 |
| six-byte RS cell (`call far` 36, `push bp` 15, `call` 23, `pop bp` 12, `mov bp,[cs:bp]` 23, `push ds/push cs/pop ds` 40, `call bp` 20, `pop ds/pop bp` 24, `retf` 34) | 227 |
| **door, RS** (`int` 71, `push bp` 15, `mov bp,sp` 8, `push [bp+6]` 33, `popf` 12, `push ds` 14, `push bx` 15, `lds` 33, `inc [bp+2]` 32, `mov bl,[bx]` 13, `mov bh,0` 8, `cmp` 12, `jae` 8, `add` 8, `mov bp,[cs:bx+]` 23, `pop bx` 12, KDS 26, `call bp` 20, `pop ds/pop bp` 24, `retf 2` 33) | **420** |
| each further kind range passed (`cmp` 12 + `jae` taken 16, the last one not taken 8) | +20..28 |
| refusal | 367 |

So a door call is **+193 clocks (40 us) over the rare cell** and +327 (69 us)
over a shim; IF is back on after ~100 clocks. Stack during the body: 10 bytes
(FLAGS, CS, IP, BP, DS) against 8 for a rare cell and 4 (+2 return) for a
shim.

### 4. DOS's AH=25h/35h (SPEC.md 96.12)

The DOS core is ONE binary for both hosts (`doscore.bin`, assembled with
`KD_BACKEND`, joined to DOS.O88 and to kern_dos), so no `%ifdef` can tell the
hosts apart in it. **`dos_vslot`**: the six-instruction walk to a vector's IVT
slot that `.setvec` and `.getvec` each wrote out, shared, and answering
`DS:dos_rarev` for AL = OS88_RARE. Both hosts virtualise 81h: AH=25h stores
the program's handler in `dos_rarev` (4 bytes in the padding before
`dos_cpm`'s `align 16`, NOT `CORE_BSS_SIZE` - which every host reserves and
which would have cost kern_dos a whole kilobyte of the program's memory,
`KD_IMG_KB` having 22 bytes of slack); AH=35h reads it back, 0000:0000 until
set, outliving the program as a DOS vector does. **The core is 2 bytes shorter
than before.** `kerndos/` needed no source change: its core IS this core.

Residual (SPEC.md 96.12): a program that writes 0000:0204 directly, or runs
`INT 81h` expecting its own handler. Under kern_dos the live 81h is the parked
kernel's door - a stale pointer only a direct reader sees. Nulling it there
(kdentry.inc, 16 bytes) was built and REFUSED: it crossed kern_dos's
`KD_IMG_KB` by the kilobyte above.

### 5. P4 - the kernel's own shims behind the door

27 `cw_` shim bodies on kern_big/emu/mega, 16 on kern_small, became RS-kind
door entries (the routine itself in `rd_tab`; a shim was `call X / retf`, 4
bytes, the entry is 2): `app_launch`, `app_close_win`, `inst_fhome_idx`,
`menu_activate`, `menu_draw_bar`, `task_spawn` (not kern_small); `cursor_hide`,
`cursor_show`, `blk_relit`, `inst_ptr`, `vid_disp_of` (kern_big's);
`inst_unmin` (WM_ANIM); `evq_mup`, `inst_find_kind`, `inst_win_owner`,
`menu_item_dis`, `menu_popup`, `mou_clamp`, `snd_beep`, `snd_disp_set`,
`wm_dmg_wins`, `wm_hit`, `wm_idx2ptr`, `wm_paint_all`, `wm_paint_dmg`,
`xm_release_rec`, `wm_win_rect`. 66 far sites became `KDOOR <routine>`; the
two `ct_` trampolines that reached two of them went (a near call is the door
site's three bytes); `fdlg.inc` grew `FDKTD` (kern_big: the door; kern_small:
FDLG.DRV's own hop, unchanged).

**Left as shims, and why**: the drawing doors a repaint calls by the dozen
(`gfx_fill`, `gfx_fill_pat`, `gfx_frame`, `font_run`, `gfx_rowbase`, the
three icon draws, `wm_clip_rows`, `vga_xor_rect_vram`, `gfx_disp_enter`,
`gfx_clip_query`, `cur_lazyrect`, `cur_unlazy`, `wm_dmg_hit`, `wm_dmg_zadd`,
`wm_zone_r`, `vid_span_wh`) - 69 us a primitive; `wm_pkgcall` (BP is its
input, which the door cannot carry); `clk_snapshot` (kern_dos's disk layer
reaches it through a stub of its own). The 23 `cw_X equ apic_X` aliases stay
far calls into their cells (T5 turns the rare ones into indices; see below).

### 6. V7 - one MartyPC reading (`make RARECNT=1`)

The knob (`Makefile`, `-DRARE_COUNTERS`) puts a 32-bit count in `.lowbss` per
rare TABLE cell (keyed by the cell's offset, in all five old bodies) and per
door index. One kern_big session under MartyPC, `os88ui` driving: boot to the
desktop; the Disk window on B:; Calculator, `12+34=` typed, closed; Note Pad,
`hello` typed; Minesweeper opened and closed; DOS.O88 off A:, `dir` and `ver`
at its prompt. Cumulative counts at the end (every rare cell not listed was 0):

| rare cell | calls | | door index (P4) | calls |
|---|---:|---|---|---:|
| `OSAPI_GFX_DESK` | 56 (16 by the boot) | | `inst_win_owner` | 97 |
| `OSAPI_SEG` | 15 (all in DOS) | | `evq_mup` | 35 |
| `OSAPI_FILE_GOTO_QM` | 9 | | `inst_ptr` | 13 |
| `OSAPI_MEM_P` | 6 | | `wm_paint_dmg` | 12 |
| `OSAPI_WM_SHOW`, `_MEM_MOVABLE`, `_WM_CREATE`, `_WM_GROW`, `_MENU_SET`, `_ABOUT_SET`, `_WM_TITLE`, `_WM_TOP`, `_WM_MINSIZE`, `OSAPI_FS_OP` | 4-5 each | | `snd_disp_set` | 5 |
| `_WM_SAVEU`, `_FILE_PATH` | 3 | | `app_launch` | 4 |
| `_FILE_DFREE`, `_MEM_FREE`, `_FILE_HERE`, `_WM_SNAP`, `_ARG_FILE`, `_FILE_READ_SEQ`, `_WM_PREFER`, `_WM_KEEPH` | 2 | | `task_spawn`, `inst_unmin` | 1 |
| `_WM_SIZABLE`, `_TASK_SPAWN`, `_PKG_REHOME`, `_MEM_CLAIM`, `_TASK_RESTARTABLE`, `_MEM_PARKSAFE`, `_WM_DISPLAY` | 1 | | | |

**What it decides for t5**:
* **Promote `OSAPI_GFX_DESK`** (the desktop's ground fill, once per damaged
  zone - a drawing door the kernel's own `cw_thm_desk` and DESK.DRV call):
  the most-called rare cell by 4x, on every window open and close.
* **`OSAPI_SEG` stays rare on this evidence**: its 15 are `os88parts.inc`'s
  part loads (each next to a disk read) and `os88con.inc`'s font window, ~7 a
  DOS command. kern_mega's per-access use is the open question the review
  named; it was NOT measured here (no Bochs in this container) and wants a
  kern_mega reading before t5 decides. It is also a cell the real-mode bracket
  keeps (TAKEN 3), so it must stay a TABLE cell anyway unless the bracket's
  door learns it.
* **`OSAPI_FS_OP` stays rare**: 4, one a file operation, each beside ~400 ms
  of disk.
* Nothing else is near hot. **Hot cells are not counted** (an 8-byte SLOT has
  no room for a counter), so K17-4's demotions are not decided by this; t5
  should demote only on a census, or leave them.
* `inst_win_owner` through the door is 97 x 327 clocks = 6.6 ms over the
  session (the Disk window's per-window ownership test); kept on the door,
  priced.

## WHAT t5 AND gates MUST AGREE ON

**The site.** A rare cell's call site emits exactly three bytes: `CD 81 nn` -
`int OS88_RARE` (`OS88_RARE equ 0x81`, already in `apps/os88api.inc`) and
`db nn`, `nn` = the cell's INDEX, its position in the kernel's `rd_tab`. No
other spelling: not `call far`, not a two-byte index. The `%imacro call` rare
arm is `int OS88_RARE` + `db <index>`; the SDK needs, per rare cell, its index
(`nn`) and a kind attribute that says "rare" (the five rare kinds need not
reach the SDK - the kernel names the kind by range - but carrying RS/RCS/RX/
RCX/RN lets `t_api_abi` check the range a cell landed in).

**The index -> kind map.** `rd_tab` is written in kind order RS (0), RCS (1),
RX (2), RCX (3), RN (4); a cell's kind is the range its index falls in:
`index < RD_B1` RS, `< RD_B2` RCS, `< RD_B3` RX, `< RD_B4` RCX, `< RD_N` RN
(equates in the kernel map). The kernel side of a renumber is moving each
`OSAPI_R*` table cell to an `RDENT RDK_<kind>, <routine>` line in its kind's
block; the SDK index must equal `rix_<routine>` - generate one from the other,
never hand-number. The P4 entries are RS and are kernel-private: put them
LAST within the RS block or anywhere - no package names them - but t5's
renumber must keep the SDK's indices and the kernel's in step. `t_api_abi`
reads `rd_tab` (a word per index; RS/RX targets `.text`, RCS/RCX/RN `.cold`).

**Rules t5 inherits** (SPEC.md 20.3.4):
1. A cell the real-mode FSX bracket KEEPS stays a table cell (today rare and
   kept: `OSAPI_SND_CAPS`, `OSAPI_XMEM_CAPS`, `OSAPI_FONT_GLYPHS`,
   `OSAPI_FILE_READ_SEQ`, `OSAPI_FSX_SPK`, `OSAPI_SEG` - `fsx_rkeep`,
   `fsx_rkeepm`), or the bracket's door learns to serve it.
2. The kernel's own `cw_X equ apic_X` aliases to RARE cells (`thm_desk`,
   `toast_show`, `wm_minsize`, `wm_snap`, `wm_onmouseup`, `wm_ondrag_c`,
   `wm_grow_paint`, `wm_show`, `wm_title_set`) become `KDOOR` sites when their
   cell moves; `tests/unit/t_toast.py` recognises `cw_toast_show` by spelling
   and must learn `KDOOR toast_show`.
3. `apic_*` labels inside the table (`fsx_rkeepm`'s `apic_xm_caps`,
   `apic_drv_pkg_call`, `apic_drv_call_at`) must survive the renumber.
4. A rare cell called with IF = 0 is fine (the restore).
5. The five old bodies go when their last six-byte cell does (-74).

## REFUSED

* **The lean door (`sti`)** - three deliberate IF = 0 callers. -3 bytes.
* **A trap gate on kern_mega** - the restore runs anyway; 0 bytes saved, a
  gate line of code spent.
* **Nulling live 81h under kern_dos** (16 bytes in `kdentry.inc`) - pushed
  kern_dos past `KD_IMG_KB`: a kilobyte off every DOS program's memory there.
* **The drawing shims on the door** (list above) - 69 us a primitive.
* **Promoting the door's own RX/RCX/RN kinds into kernel traffic as a live
  proof** (e.g. `cw_drv_pkg_call` as RX): +~18 resident bytes for a proof;
  the four kinds were checked by disassembly instead and run first in t5.

## WANTS THE OWNER

* Nothing blocking. For information: the door spends +193 clocks a rare call
  over today's cell (PREDICTED; the T5 row the owner accepted priced +150 to
  +300). Its MartyPC reading belongs in the final pass.

## WANTS AN ABI BREAK (found, not taken)

* **`call COLD_SEG:x` from `.text` and the modules through an RCS door
  entry**: the RCS kind takes any far-called cold body with DS = KERNEL and no
  BP input. Each such site is 5 bytes against 3, each distinct target +2 in
  `rd_tab`. Kernel-private, no bump. Not counted here; a wave-C sweep with a
  per-site cycle judgement (+~190 clocks a call - not on a draw path).

## ROWS (the final pass's union for this branch)

Every row boots a kernel whose `.text`/`.cold` moved, so the reach is wide;
the rows ABOUT what moved:

* the door and DOS's vector: `dosvec`, `dosdir`, `dosdirsw`, `kerndos`,
  `kdreturn` (and its `kdreturn*` family), `mega-dosdir`, `mega-dosvec`
  (`tests/megaport.txt`);
* app launch / close / instance store (`app_launch`, `app_close_win`,
  `inst_*`): `uilayer`, `small128`, `ddsmall`, `tmsmall`, `tmload`,
  `tmowner`, `fdlgdrop`, `fdlgsmall`, `fdlgchsmall`, `fdlgchoose`, `fdlgup`,
  `fdlggrey`;
* menus (`menu_*`, `mou_clamp`): `uilayer` and the desk/sticky menu rows
  `stikmenu`, `deskkeys`, `deskmodal`;
* screen saver (`cursor_*`, `blk_relit`): `saver`, `savervga`, `saverate`;
* dock / desk layer / sticky apps / extended desktop (module sites):
  `dockmodule`, `dockmark`, `dockpos`, `deskdock`, `desksc`, `desklayer`
  (`deskz`, `deskclip`, `deskwhole`, `deskground`, `deskclose`), `stik`,
  `stikboot`, `stikswap`, `stikhiber`, `extdmod`;
* Control Panel (CTRL.DRV sites): `cpup`, `cpcost`, `cpnames`, `cplistrow`,
  `cpmcache`;
* hibernate (HIBER.DRV sites, the IVT carrying 81h): `hibernate`,
  `hibernatedrv`, `hibernatem`;
* the mega bracket's bound write: the kern_mega FSXF_REAL rows in
  `tests/megaport.txt` (any `fsx`/`real` port);
* `task_spawn` from a driver worker: the sound-driver rows that spawn one.

**Run here** (host checks on all four kernels; MartyPC/QEMU by hand, none as
a soak): `make` (fast tier, 77 passed) on kern_big after the final source;
`make small`, `make emu`, `make mega` green; `python3 tests/dosvec.py` **ok**,
`python3 tests/dosdir.py` **ok**; a scratch probe (`RAREV.COM`: AH=35h 81h,
AH=25h 81h, AH=35h 81h, then the live IVT) on kern_big under MartyPC printed
`R0=0000:0000 R1=1FD0:014F LV=0060:9B83` - the live vector is
`os88_door` - and on the build before the virtualisation it printed
`LV=1FD0:014F` (red, then green); kern_small booted under MartyPC, opened and
closed Calculator and Minesweeper through the door's launch and close;
kern_emu and kern_mega booted under QEMU (a 386: kern_mega in protected
mode) and opened the Disk window (door entries `inst_win_owner`,
`wm_paint_dmg` on that path); the V7 session above on a RARECNT=1 kern_big.

## DEFECTS FOUND

None in the tree. In this work: the first DOS arm used `%ifndef KD_BACKEND`,
which cannot work in a core assembled once for both hosts - the probe's
`LV=` line caught it (the live vector was the program's).
