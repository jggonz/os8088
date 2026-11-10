# Kernel size pass 19: gates (the flag day's host gates, and T6's call spelling)

The **gates** agent's record (BRIEF.md section 1, wave A). Branch
`ksp19-gates`, cut from `174b80bf`. Owned: `tests/unit/t_api_abi.py`,
`t_mirror.py`, `tools/abilock.py`, `tools/os88pkg.py`/`os88drv.py`'s scans,
the kind-attribute generator (new: `tools/os88cells.py`) and the call-macro
region of `apps/os88api.inc`. Also taken, as files in no row: the new
`kernel/xmabi.inc` and the edits it needed in `kernel/xmem.inc`,
`drivers/xmem/xmem.asm`, the Makefile's `xmem.bin` rule and three soak
scripts that assemble XMEM.DRV by hand (`xmcheck`, `xmhwalk`, `vidxms`).

## BYTES - none, proven

Every row here is a gate or a mechanism, so the claim is **no shipped byte
moved**, and it was checked rather than argued. `build/` was hashed (md5,
every file) after a full baseline of the brief commit's sources - `make`,
`make small emu mega`, `make cword runcpm c64 apple2 paccman lemmings weave
loom` - and again after the change, built the same way, at ONE commit (no
commit between, so the build number did not move):

| what | identical |
|---|---:|
| `.bin` (packages, drivers, parts, all four kernels' `kernel.bin`) | 300 |
| `.o88` | 109 |
| `.drv` (drivers and kernel modules) | 61 |
| `.img` (every disk `all`, `small`, `emu` and `mega` build) | 45 |

The only files that differ are `.lst` listings and `.map` files, and only in
NASM's macro-local label NUMBERS (`..@27.o` -> `..@109.o`): every `call` now
passes through a macro instance. Plus 65 test packages under `tests/*/`
(not in `all`) assembled with the old and the new SDK: 65 identical.

**kernsize: the brief's 0.2 table, unchanged on all four kernels** -
`kernel.bin` is byte-identical in `build/`, `smallk`, `emuk` and `megak`.
No span moved. Packages: 0 bytes.

## TAKEN

1. **T6, mechanism (b): `call` overloaded once in `apps/os88api.inc`**
   (SPEC.md 20.3.3, new). 0 bytes. What NASM does, checked first under
   `-w+error` on nasm 2.16.01:
   - an `%imacro call 1` shadowing the instruction draws **no warning**; a
     `call` inside its own body is the instruction (no recursion), and a
     wrapped `call %2` reaches it like any other call;
   - **a macro's parameters are smacro-expanded BEFORE the macro sees
     them**, so `call OSAPI_WM_SHOW` arrives as `KERNEL_SEG:0x007C` and the
     NAME is gone - `%ifdef %1_KIND` cannot work. So the kind rides IN the
     expansion: a generated block re-expands each rare name as
     `__OSAPI_RARE KERNEL_SEG:0x007C` (`%xdefine`, after the `%define`), and
     the macro tests the first 13 characters of `%defstr` of its operand.
     That also makes every macro-wrapped site (`os88thunk.asm`'s CC_T_*
     makers, `os88ui.inc`, `w16pm.inc`, `stiklib.asm`, `rdgppu.inc`, ...)
     carry the mark with no edit;
   - **overloading by parameter COUNT (`__OSAPI_RARE, addr`) was tried and
     refused**: a wrapper with a fixed count (`CC_T_A1 name, OSAPI_X, al`)
     then sees one argument too many (`pp-macro-params-multi`);
   - an `%imacro` is removed with `%unimacro`, not `%unmacro`.
   `__OSAPI_RARE` is defined only inside the arm, so a rare name under
   `jmp`/`dw` does not assemble (flagday.md T6's point (c)). The rare arm is
   `OS88_RARE_CALL`; `OS88_RARE_DOOR` 1 selects the door body already
   written as `int OS88_RARE_VEC / db %1` (both T5's names, defined
   nowhere yet). Assembly cost: `sheet.asm`, the largest source, 3.1 s
   before and after (noise). Source gates that parse `call OSAPI_X`
   (t_textrules, t_movable, ...) need nothing: the spelling did not change.
2. **The CELL KINDS generator, `tools/os88cells.py`.** The table decoder
   moved out of t_api_abi into it (one decoder for the gate and the
   generator); `--write` regenerates the blocks at the end of
   `apps/os88api.inc` (111 rare cells) and `drivers/os88drv.inc` (1,
   `OSAPI_DRV_TASK`, published after the include); `--list` prints the
   table. Source: kern_big's decoded table. **t_api_abi's check 7** fails
   `make` when a block is stale.
3. **The surviving-far-call gate** (`os88cells.farcall_problems`, called by
   `os88pkg.py` over the image and every `--part`, and by `os88drv.py`):
   - ARMED NOW: a far call to `KERNEL_SEG` at an offset inside the table
     that is no published cell's first byte (mid-cell). Zero hits in every
     package, driver and part in the tree;
   - **past the table it judges nothing** - first armed that way it refused
     `DOS.O88`: `kerndos.bin`, one of its parts, far-calls the kernel's disk
     layer at private offsets (0x4EFD..0x781D) by design, which
     `t_kdfar.py` polices;
   - ARMED BY `OS88_RARE_DOOR` 1 (T5's switch): a far call to a cell the
     kind block marks rare. `OS88_RARE_GATE=1` arms it early; the new row
     uses that to watch `mines` get refused.
4. **abi-3 / gkm-2: `t_api_abi` over all four kernels.** Default (fast):
   kern_big, plus each other tree whose `kernel.bin` is current, a stale or
   missing tree NAMED; `--all` (new full row `api-abi-all`, `wants=` the
   three trees) fails on one. kern_small and kern_emu through os88sym's
   PROVEN map (`$OS88_BUILD` and their defines); kern_mega through its
   build's own map (os88sym's `_mega_map` argument). New decode: kern_mega's
   stub shapes (`1E E8 .. 1F CB 90 90` SLOT, `1E E9 .. 90x4` FJSLOT,
   `E9 .. 90 [90] CB` MEGA_ICW/ICWB, each followed through its stub to the
   routine or the store) and the NOP cell. Per-kernel LEDGER rows, never a
   skipped kernel: `KERNEL_OWN` (mega: 4 setters and 3 protected-mode
   questions as rare bodies; small: ONDRAG/ONTIMER's refusals),
   `KERNEL_NOP` (small: BLIT1_PEN, THM_SET, WM_ONWHEEL), `KERNEL_FIXED`
   (small: FS_OP and VOL_CHECK answer AX=1/CF=1 inline - gkm-2's "the
   refusal names its contract", as bytes), `KERNEL_ALIAS` (small: GFX_DESK
   is gfx_fill_gray). 4,307 checks over the four, 0.3 s warm. "Ready for
   T5's index table": the decoder is one module, and the kind-block check is
   where the index table's decode plugs in - its shape is T5's and is not
   guessed here.
5. **engsys-11: the C SDK.** `abilock`'s PKG_FMT surface adds
   `apps/cc/os88.h` (C comments stripped, `surface_c`) and
   `apps/cc/os88thunk.asm`. t_mirror reads every `#define OS88_<X> <value>`
   through a prefix map (`<X>`, `OSAPI_<X>`, `C<X>`), six `C_ALIAS` rows
   (`MENU_MAX`->`MENU_APPMAX`, `SCAN_*`->`KSC_*`, `ASSOC_*`->`ASSOC_FL_*`)
   and a `C_ONLY` list with reasons (the guard, `BLITP_PROBE`,
   `HUGE_W32/W16`); an unmapped new constant fails. 79 compared, all agree.
6. **drivers-16: the private ABIs.** `abilock` guards `VX_ABI_VER`
   (`kernel/vxabi.inc`), `XM_ABI_VER` and `HD_ABI_VER`
   (`drivers/hdd/hddabi.inc`), recorded at 5, 2 and 7. XMEM's mirrored pair
   (XM_ABI_VER, 7 XMV_*, 6 XMC_*) is ONE include now, `kernel/xmabi.inc`,
   vxabi.inc's shape (the driver's rule gains `-I kernel/`); XMEM.DRV and
   the kernel byte-identical.

`abilock --update` recorded once, no bump (the SDK's text moved; nothing
built before stops running). SPEC.md 20.2.0.1 says what each guard covers.
The lock WILL conflict at merge; re-run `--update`.

## Red arms (each broken on purpose, each went red, each restored)

- a rare line deleted from the kind block -> api-abi: "rare on the kernel,
  not marked: OSAPI_WM_SHOW";
- the macro's prefix test misspelt -> cellgate: the rare calls stop
  assembling (`call __OSAPI_RARE ...`); the test made always-true -> the
  marker lands on 11 calls, not 4;
- a KERNEL_OWN row removed -> `api-abi --kernel mega` red;
- `OS88_FT_DIR` 2 -> 9 in os88.h -> mirror red;
- the lock: t_abilock's mutations (an os88.h prototype, a `#define`, a
  thunk's cell, a code line of each private ABI) move the fingerprint; an
  os88.h comment and each guard's own line do not;
- the gate: synthetic mid-cell and rare far calls refused, a past-table one
  not; `mines` packs unarmed and is REFUSED armed early.

## REFUSED / WANTS THE OWNER / WANTS AN ABI BREAK

None of the three.

Found and RECORDED, not taken: `THMV_ABI_VER` (kernel/vga12.inc and
drivers/thmpal/thmpal.asm) is XMEM's old shape exactly - one private number
typed twice, t_mirror holding the copies together, not in the lock.
drivers-16 named only VX/XM/HD. 0 bytes either way.

## FOR T5 (wave B) - the flip, concretely

1. `%define OS88_RARE_DOOR 1` in `apps/os88api.inc` (it is `%ifndef`-
   guarded, so `-D` works too), and define `OS88_RARE_VEC` (the door's).
2. `os88cells.render()` writes `__OSAPI_RARE <index>` in place of
   `__OSAPI_RARE OSAPI_X`; the index is the cell's position in T5's rare
   set, read out of the kernel's index table (teach `os88cells.walk` /
   `kernel_table` that table, and `stale()` to compare indices as well as
   names).
3. The rare arm's door body is already `int OS88_RARE_VEC / db %1`; edit
   that one macro if the door wants another shape.
4. Both gates arm themselves from `OS88_RARE_DOOR`. If the rare `%define`s
   keep their `KERNEL_SEG:` address, the gate refuses a far call to it; if
   they lose it, rare cells are no cells and the mid-cell rule is what is
   left. Both are consistent.
5. `t_cellgate`'s "as shipped" arm compares with a twin of bare far calls;
   after the flip its twin spells the rare calls `CD vec idx`.
6. **Rare-ness differs per kernel**, and one package binary serves every
   kernel: kern_small has FS_OP and VOL_CHECK FIXED (rare on big) and
   ONDRAG/ONTIMER rare (inline on big); kern_mega has 7 more rare cells (4
   window setters - ONMOUSEUP, ONCLOSE, ONDRAG, ONTIMER - plus XMEM_SEL/RUN
   and MEM_CODESEL). `t_api_abi --all` lists them every run. A cell hot in
   the SDK must stay a far-callable table cell on every kernel; a cell rare
   in the SDK must answer through the door on every kernel.
7. The kind block carries only "rare" (with the kind name as a comment);
   the door agent decides what it must carry, and `render()` writes it.
8. SPEC.md 20.3.3 is this section's number; the door's text needs another.

Also: `tools/os88wire.py` hard-codes `KERNEL_SEG = 0x0060` for its far-call
scan, which L1 moves; `os88cells.sdk_switch()` reads KERNEL_SEG out of the
SDK instead.

## ROWS

Host, reachable: `api-abi`, `cellgate` (new), `abilock`, `mirror`,
`registry`, `docindex`, `checkdocs` (fast); `api-abi-all` (full, new).
Soak rows whose SCRIPTS changed (`-I kernel/` for xmabi.inc): `xmhwalk` and
`xmla` (both run here: ok), `xmcheck` and `vidxms*` (their assembly lines
run by hand: ok; the rows not run). No emulator row can see anything else:
every artefact they boot is byte-identical. Every package and driver build
now passes through the gate, so any row that packs with os88pkg/os88drv
exercises it.
