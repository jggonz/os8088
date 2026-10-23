# ksp13 agent "desk" - the desk layer and Sticky Apps

Kernel size pass 13 (the brief on this branch still sits under `ksp14/`; the
coordinator renumbered the pass after it was written). Concept: the desk
layer, `DESK.DRV` and Sticky Apps (SPEC.md 110, 11.103), commits `0a662504`
(#251) and `b1ea9155` (#263), all on `main` and so FROZEN this pass:
`OSAPI_WM_DESK`, `OSAPI_GFX_DESK`, `OSAPI_CLK_GET`, `WF_DESK`/`WF_HOVER`,
`OS88_F_DESK`. Owned: `kernel/deskmod.inc` (`DESK.DRV`'s `.modj` and the
resident `osapi_wm_desk_x` / `desk_need`), the desk hunks in `wm.inc`,
`mouse.inc`, `mod.inc`, `dock.inc`, `dockmod.inc`, `desk.inc`, `clock.inc`
and `fdlg.inc`, `stik_boot` in `desksc.inc`, and the routines those hunks
sit in. NOT touched: `desksc.inc`'s link/autostart hunks (agent links), and
the desk hunks in `ui.inc` and `loader.inc`, which the brief lists under
agent misc (`ui_task` +30, `ui_drag` +11, `loader.inc`'s other +52) - what
I would do there is in CROSS-FILE. Branch `ksp13-desk` (cut as
`ksp14-desk` at `aedadd25`). Figures are ASSEMBLED bytes, `tools/kernsize.py
--json` absolutes, resident = `.text` + `.bss` + `.cold` + `.lowbss` +
`.vgabuf`; modules are the UNPACKED image (`os88drv.image_unwrap`).

## BASE AND TIP

Base `aedadd25` (`4833dfef` plus the brief).

| | base | tip | delta |
|---|---:|---:|---:|
| kern_big resident | 92,757 | 92,741 | **-16** |
| kern_big `.text` / `.bss` / `.cold` / `.lowbss` / `.vgabuf` | 43,756 / 5,223 / 38,084 / 5,358 / 336 | 43,750 / 5,223 / 38,074 / 5,358 / 336 | -6 / 0 / -10 / 0 / 0 |
| kern_big `.boot2` / `.ovl` / `KERN_SIZE` | 5,129 / 5,106 / 98,816 | same | 0 |
| kern_emu resident | 93,016 | 93,000 | **-16** (`.text` -6, `.cold` -10) |
| kern_small resident | 60,260 | 60,258 | **-2** (`.cold` -2: `OSAPI_CLK_GET`) |
| `DOCK.DRV` (`.modk`) image | 2,257 | 2,247 | **-10** (packed file 2,054 -> 2,050; still a 3 KB claim) |
| `DESK.DRV` (`.modj`) image | 235 | 241 | +6 (stored, one sector, inside the same 1 KB claim - so no heap and no disk cluster moved) |
| every other module, driver, `.boot2`, `.ovl` | | | 0 |

The concept's own kern_big resident was ~+150 at the base (the brief's
figure: `wm.inc` +81, `deskmod.inc` +36, `clock.inc` +32, `mouse.inc` +16,
`desk.inc` +11, `dock.inc`, `mod.inc`, `stik_boot` +66). **-16 is ~11% of
it.** What is left is mostly forced: eleven `WM_CHROMELESS` word tests
(+1 each against the byte test, because WF_DESK is in the high byte and the
low byte has no free bit), four one-line `WF_DESK` arms in the window
system's z-order and clip paths, `DESK.DRV`'s 16 bytes of slot block and its
row, the hover edge, and the two public cells' bodies. The two items that
would take a real bite out of the rest change behaviour and are in WANTS THE
OWNER (-63 and -10).

## TAKEN

All on kern_big and kern_emu unless said otherwise.

| item | file | big/emu | small | how |
|---|---|---:|---:|---|
| `wm_top`'s visible-and-not-desk test | wm.inc | -1 | 0 | `and ax, 0x0402` then `dec ax` / `dec ax` / `jz` in place of `cmp ax, 2` / `je`: 0, 2, 400h, 402h, and only 2 reaches zero. 4 clocks either way, a byte less to fetch |
| `OSAPI_CLK_GET`'s body | clock.inc | -2 | **-2** | the second as AH of a word read at `clk_sec - 1` (the 3-byte accumulator form where `mov ah, [m]` is 4), and the `clc` dropped: after the `popf` the `or al, [clk_rtc]` that assembles AL is what leaves CF = 0. The two settings are read after the `popf` - neither is a field a rollover tears. The contract is CF = 0 and nothing else about flags (`apps/os88api.inc`, SPEC.md 37.96, whose "two words and three bytes" sentence is updated) |
| `desk_live`'s Sticky App mark | desk.inc | -1 | (kern_big only) | `mov al, [es:bx+3]` / `xor al, 'D'` / `cmp al, 1` in place of `cmp` / `stc` / `je` / `clc`: +3 is `\`, 0 or `D`, AL is 0 exactly for `D`, and `cmp al, 1` borrows only from 0 - CF = 1 dead, the same CF the `sbb` below has always read |
| `db_key`'s "live and not a widget" | dock.inc | -2 | 0 | I_STATE and I_FLAGS read as ONE word (`mov ax, [di]` / `and ah, IF_DESK` / `dec ax` / `jnz .none`), an assertion pinning I_STATE = 0 and I_FLAGS = 1, and a three-byte `.none: xor ax, ax / ret`. kern_small keeps its two-instruction test |
| `db_hit` asks `db_key` | dock.inc | -2 | 0 | `push ax` / `call db_key` / `or ax, ax` / `pop ax` / `jnz .in` in place of the second copy of the state and desk tests: "which records have a tile" is ONE question (SPEC.md 29.2 rule 3), and a live key is never 0 (`db_key`'s `inc ax` after its `and`). AL, the slot, is kept for `db_rclick` |
| `stik_boot`'s two early-outs | desksc.inc | -6 | (kern_big only) | the `kentc_bp` bank FIRST, then `[ui_rebootq]` and `jcxz` on the row count, both leaving through the one `jmp kretc_es`: the `cmp byte [sc_nrows], 0` / `je` and the separate `ret` go. A boot pays the bank once |
| `fdlg_top_x`'s widget arm | fdlg.inc, deskmod.inc | -1 | 0 | `wm_top`'s answer is put in BX BEFORE the far call to `dkd_key`, which now REPLACES it (CF = 0, the widget) or KEEPS it (CF = 1) - which is exactly what `mod_gone` does with no image, keeping every register. The arm is `jz .ask` / `.ask: xchg ax, bx / call far / pop ax / retf` and shares the small build's `xor ax, ax` / `.stand`. `dkd_key` banks SI to keep BX: `DESK.DRV` +6, inside its 1 KB claim. SPEC.md 11.103.4 and the 11.103.10 slot table say so |
| `wm_hit`'s `mov ah, 0` -> `cbw` | wm.inc | -1 | 0 | `[wm_zn]` <= 12, so AH = 0; two clocks faster. **Taken for parity, on kern_big only**: `wm_top`'s byte left `wm_paint_all` and `ui_task` one byte odd, and this puts every hot routine back at an even shift. Against not taking it, it bought parity and no byte: at the odd shift `dsk_img16`'s jump had relaxed for a free byte, which the even shift gives back (`.text` is -6 either way). On kern_small `wm_top` did not move, so neither does this (`%ifdef`) |
| `DOCK.DRV`: `dkx_key`, `dkx_hit`, `dkx_map` | dockmod.inc | image -10 | | `dkx_key` the same one-word test, with `or di, di` / `jz .none` replacing its leading `xor ax, ax` (-2 net of the new `.none`), `dkx_hit` asks `dkx_key` as `db_hit` asks `db_key` (-2), and `dkx_map`'s count and walk loops the one-word test in AX and CX, both free there (-6) |

**Cycles.** Nothing on the brief's hot list changed. `wm_top` (per key and
per raise) is the same 4 clocks for `dec ax`/`dec ax` as for `cmp ax, imm16`
and one byte less to fetch; `wm_hit`'s `cbw` is 2 clocks against 4. The rest
run once per click, per dock paint or once per boot.

**Parity**: `tools/os88rdiff.py kernel aedadd25 . --hot` over `gfx_blit4`,
`gfx_blit1*`, `font_run`, `mou_isr`, `mou_p2_isr`, `kbm_isr`, `sch_isr`,
`sch_switch`, `dsk_xfer`, `ui_task`, `dsk_runcap`, `dsk_next_clus_x` and
`wm_paint_all`: **all 14 identical at an even shift on kern_big, all 13 on
kern_small** (no `mou_p2_isr` there). The `.cold` run `fdlg_hook ..
desk_live` sits at -3 (odd) on kern_big; nothing on the hot list is in it.

**Equivalence.** Every change is a reading, enumerated where there is a
value space: `wm_top` over the four values of `W_FLAGS & 402h`;
`desk_live` over the three values +3 can hold; `db_key`/`dkx_key`/`dkx_map`
over I_STATE 0..2 x I_FLAGS bit 2 (only state 1 without the bit gives AX =
1); `db_hit`/`dkx_hit` over the same, since a live key is never 0;
`fdlg_top_x` over its three exits (gone: BX = 0; no widget, with or without
the image: BX = wm_top's answer, CF = 1; widget: BX = it, CF = 0 - the same
CF as before on each, the other flags untouched by the `xchg`/`pop`/`retf`
tail). `stik_boot`'s registers are restored on every path by the one
epilogue. `tools/stkbalance.py` over every file touched, base against tip:
identical output (one pre-existing line on `stik_boot`'s tail `jmp kretc_es`
at depth +1, which is `kentc_bp`'s +7 net bank, as before).

## REFUSED

| item | bytes | why |
|---|---:|---|
| `wm_show_b`'s desk arm written resident (visible bit, `wm_win_rect`, `wm_dmg_npro`, `wm_paint_dmg`, sharing `wm_hide_b`'s tail) | +8 | the far call to `dkd_show` (5) plus its 4-byte slot is 9; the cheapest resident spelling is 17 |
| `wm_hide_b`'s `wm_dmg_npro` arm (11) into the image | -1 +4 `.bss` | a slot is 4 bytes of `.bss` and the call 4; the arm's body is a single 5-byte store |
| `DKD_HOVER`, `DKD_SHOW`, `DKD_KEY` folded into `dkd_verb`'s AL dispatch | +4 a site, -4 `.bss` a slot | each site would need `mov al, n` and a bank of AX it does not have today |
| `wm_su_rect`'s desk arm as `xchg si, di` / call / `xchg` | 0 | 8 bytes either way |
| `wm_clip_sub`'s chromeless test as a DX = 0/1 flag into `wm_clip_subg` | +1 | the jump to `wm_clip_subf` is shorter |
| `wm_draw_win`: one `WM_CHROMELESS` test with the desk/fullscreen split behind it | +1 | the fullscreen arm fills white and has no `WF_OWNBG` opt-out, so the split is still a test |
| `wm_lift`'s desk test moved up into `wm_front_b` | 0 or worse | `wm_front_b`'s `.slow` (a hidden window) reaches `wm_lift` too; the one test in `wm_lift` is the cheapest single point |
| the hover edge in `cur_shape_pass` via DX, or `dkd_hover` handing BX back | 0 / breaks | the DX spelling is the same 10 bytes; handing BX back is wrong with no image, where `mod_gone` would return the OLD window and the shape pass would dress the pointer for it |
| `desk_need`'s pair save as two overlapping word pushes (`[drv_vol_cwd]`, `[drv_vol_cwd + 1]`) | 0 | 24 bytes either way |
| `stik_boot` into `DESK.DRV` | ~-30 | a fallback loop must stay resident for a machine with no `DESK.DRV` on its disk, and it would load and pin the 1 KB image at boot for a Sticky App row whose package is missing - a behaviour change for less than WANTS 1 buys |
| `dkd_key` skipping its `stc` with a `db 0A8h` (`test al, imm8`) | -2 image | the image is inside its 1 KB claim and one sector either way; not worth an opcode trick |
| `WF_DESK` moved into W_FLAGS's LOW byte, making every `WM_CHROMELESS` a byte test again | ~-11 | there is no free low bit (bits 0..7 are used, used/visible/sizable/full/nosnap/saveu/ownbg/keeph), and `WF_DESK` is in `main`'s `apps/os88api.inc` |

## WANTS THE OWNER

1. **`stik_boot` into the boot blob - -63 resident on kern_big and kern_emu
   (`.cold` -63), `.boot2` +78 (5,129 -> 5,207 on kern_big, 5,229 -> 5,307 on
   kern_emu; `BOOT2_SECS` stays 11, 5,632 bytes).** MEASURED: built and
   reverted. The loop moves to `section .boot2` and reaches `sc_row`,
   `sc_open` and `desk_reflow` through `wm_cbd` (`mov bp, x` / `call
   COLD_SEG:wm_cbd`, `as_boot`'s own idiom); `kmain_o` does `BLOBCALL
   stik_boot` immediately before `BLOBCALL as_boot`, and `drv_notice_x` loses
   its call. **What the user sees changes**: the widgets are relaunched
   BEFORE the first desktop frame instead of after it, so the desktop
   appears later by the widgets' load time and comes up with them already
   on it (today it appears, then they arrive); and they relaunch before
   autostart's programs instead of after. Two smaller differences: the
   launch runs while the blob still holds the heap floor (`BOOT2_PAD`), as
   `as_boot`'s launches already do, so a widget that only just fits on a
   128 KB kern_big machine could be refused; and a NOT FOUND toast is shown
   before the first paint rather than after. SPEC.md 110.4's "after the
   first desktop paint" would be rewritten. The kmain_o line is next to
   agent links' `as_boot` and `kernel.asm` is wkcore's, so whoever takes it
   coordinates the two.
2. **`wm_su_rect` answers a CHROMELESS window's frame, not just a desk
   window's - -10 on kern_big and kern_emu.** MEASURED. Its desk arm becomes
   `WM_CHROMELESS di` (+1), and `wm_cov_rect`'s fullscreen fix-up (`test
   WF_FULL` / `mov si, di` / `call wm_win_rect`, 11 bytes) goes on kern_big.
   It is also a **defect fix**: today `OSAPI_WM_DAMAGE` on a FULLSCREEN
   window answers CF = 1 with AX..DX inset by a title bar and borders the
   window does not have, where the contract says *"CF=1 = draw the WHOLE
   content and AX/BX/CX/DX are it"* - so a fullscreen package that trusts
   the rect skips its top 18 rows. It also widens a fullscreen window's
   raise cache (`wm_su_flay`) from that inset to the whole frame. Both are
   changes to what a fullscreen program sees, so it is the owner's; on
   kern_small it would cost +3 and is not proposed there.

## WANTS AN ABI BREAK

None that pays. The one that would - `WF_DESK` in W_FLAGS's low byte, ~-11
across the `WM_CHROMELESS` sites - has no free bit to go to (REFUSED).

## CROSS-FILE

* **`osapi_wm_desk_x` and `desk_need` as one far body - -3 on kern_big and
  kern_emu, needs one line of `loader.inc` (agent misc's step-5 desk
  hunk).** QUERY's `call desk_need` / `retf` (4) goes if `desk_need`'s body
  IS the cell's AL = 0 arm, ending in `retf`; the loader's step 5 then calls
  it `push cs` / `call desk_need` (4 against 3) - both are `.cold`.
  ESTIMATED.
* **`tools/os88rdiff.py` cannot build any commit newer than
  `drivers/thmpal/thmrows.inc`** (`kernel/vga12.inc:173: unable to open
  include file`): `vga12.inc` includes it by a ROOT-relative path, and
  `_fetch` searches only the source's directory and the `-I` list, never the
  export's root, where nasm itself looks. The fix is one line - `dirs =
  [os.path.dirname(self.src), "."] + ...` - and every number in this note's
  parity section was taken with it applied. NOT committed here, because
  every agent of this pass runs the tool and two fixes to one line would
  conflict at the merge; the coordinator should take it once.
* `ui.inc` and `loader.inc`'s desk hunks (misc's): read, nothing found
  beyond the item above. `ui_task`'s content-press arm needs its `mov si,
  bx` because `.content_front` reads SI.
* A hazard by READING, not reproduced: `drv_vol_bank` keeps one static
  pair on the rule that brackets never nest, and `desk_need` exists partly
  to protect `sc_open`'s outer pair from `mod_need`'s inner bracket. Any
  OTHER `mod_need` reached inside an `sc_open` launch - a package opened
  from a desktop shortcut whose entry proc causes a module load (on
  kern_small, `FDLG.DRV`) - would leave the machine standing in the
  package's folder rather than where the user was. A general fix (the pair
  saved in `mod_need`) costs ~16 there and would let `desk_need` lose its
  16-byte save, so it is about byte-neutral; wkfs/wkcore's if anybody's.

## ROWS RUN

On the tip, once, after the last code commit, `--marty-jobs 1`:
`deskz deskdock deskkeys deskhover deskclose deskdrag stikboot stikmiss
stikhiber stiknodesk deskcalgrid dockmark dockpos dockmodule deskmodal
fdlgchoose wmchrome desksc deskitem`: **18 ok, 1 FAIL** - `desksc`, PRE-EXISTING:
`tools/os88bisect.py classify --base aedadd25 desksc` fails at the base on
the same leg. Its leg G waits for a Disk window whose folder is cluster 4,
hard-coded, and `MEDIA` is cluster 5 on this tree's `apps360.img`
(`tools/os88fat.py ls`) - the window it wanted did open, titled MEDIA, with
FS (1, 5, 1). The row's own `media = ui.entry("MEDIA", root)` is computed
and never used; it is agent links' subject (shortcuts), so it is reported
and not touched. `stiknodesk` and `deskmodal` first SKIPPED (the box had no
mtools); with `apt-get install mtools` both ran: ok.

**Broken on purpose**: `db_key`'s and `dkx_key`'s `and ah, IF_DESK` made
`and ah, 0` (a widget gets a tile) - `deskdock` went red on four checks
(the tile-11 rect is not bare strip, a click there fronts it, the bar
leaves Locator, the tile stays); restored, green.

Every `make` ran the fast tier (47 rows, green); `make small`, `make emu`,
`checkdocs` after `git add` at each commit.

## DEFECTS FOUND

* `OSAPI_WM_DAMAGE`'s rect for a fullscreen window (WANTS 2) - found by
  reading `wm_su_rect`'s callers; not fixed, it is a behaviour change.
* `tools/os88rdiff.py`'s include search (CROSS-FILE) - found running the
  parity check; not committed.
* `tests/desksc.py` leg G's hard-coded cluster 4 for `B:\MEDIA` (ROWS RUN)
  - fails at the base; not fixed (agent links' row).
* `os88test.py`'s `stiknodesk` and `deskmodal` need `mtools`, which `make
  deps` does not install in this container, so they SKIP silently in a
  soak - a skip is the box declining to answer.
