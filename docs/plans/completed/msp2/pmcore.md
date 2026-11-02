# kern_mega size pass 2: pmcore

Owner of `kernel/mega.inc` lines 1-5019 at the base (`ead16c61`): the
switch, the tables, the ROM halves, reflection, `mega_tell`, the fault
reporter and the #GP fixups. Branch `msp2-pmcore`. The brief is
[BRIEF.md](BRIEF.md); pass 1's record is
[../msp1/pmcore.md](../msp1/pmcore.md), and this pass started from its
DEFERRED list.

This pass found no large item of the kind pass 1 had (`.text` code that
only ever ran in protected mode). What it found is small duplication:
**the reflection's real-mode window still had hand-written copy loops and
per-case stores, and `.cold` held three hand-written copies of the same
GDT descriptor store** that `mega_gdtaim` already did.

## BASE AND TIP

`tools/kernsize.py --build build/megak --json`. Base is the coordinator's
`/tmp/msp2/base-mega.json`; tip is `c1df8ff9`.

| kern_mega | base | tip | delta |
|---|---:|---:|---:|
| `.text` | 46,579 | 46,521 | **-58** |
| `.bss` | 5,427 | 5,427 | 0 |
| `.cold` | 46,165 | 46,088 | **-77** |
| `.lowbss` | 6,751 | 6,751 | 0 |
| `.vgabuf` | 336 | 336 | 0 |
| `.mega` (+scratch) | 21,440 (+1,490) | 21,392 (+1,490) | **-48** |
| `.boot2` / `.ovl` | 6,709 / 5,118 | 6,709 / 5,118 | 0 |
| `KERN_SIZE` at boot | 134,656 | 134,656 | 0 (no rung crossed: the bytes are real all the same) |
| conventional bytes after the switch (`.text`+`.bss`+`.lowbss`+`.vgabuf`) | 59,093 | 59,035 | **-58** |
| `kernsize` footprint line (rung) | 65,024 | 65,024 | 0 |
| `.text`+`.bss` of the 64KB window | 52,006 | 51,948 | -58 |

Net across sections: **-183 bytes**, 58 of them conventional memory
(below 1MB for ever) and 125 above 1MB after the switch.

**kern_big, kern_small, kern_emu: byte-identical.** Every change is inside
`mega.inc`'s `%ifdef KERN_MEGA` (line 63 to the end of the file);
`kernsize --json` for kern_big is equal to `/tmp/msp2/base-big.json` in
every field, and kern_small and kern_emu assemble no line of the file. The
knob arms assemble: `-DMEGA_DIAG`, `-DMEGA_DIAG -DMEGA_E9`, `-DPIC_LOSE`,
`-DMEGA_TRAPLOG -DMEGA_E9`.

## TAKEN

The section totals in BASE AND TIP are assembled. The per-item figures
below are hand counts of the encodings, so they are ESTIMATES and do not
sum exactly to the totals (`.text` items sum to -56 against -58
measured, `.mega` items to -39 against -48). The rest is jump relaxation
in the edited routines. No other kernel changes.

1. **`mega_capoff`: two `rep movsw` runs** instead of a 6-cell loop called
   twice and four moves for int 33h. `mega_c33sv` follows `mega_capsave`, so
   the int 33h cell is two `movsw`s more (an `%error` checks the layout).
   `.text` **-30**, and faster: one `rep movsw` per run in place of seven
   instructions per cell. It runs in the reflection's real-mode window after
   the BIOS has returned. The BIOS's FLAGS are already at +20 in the frame
   and the caller's are at +28, so the `cld` it adds changes no flag
   anything reads.
2. **`mega_cap2`'s six capture stubs are five bytes each** (`jmp short
   mega_capc` instead of `jmp near`; the last one is a jump to the next
   byte, so every line is the same length), and `mega_capon` steps
   `add di, 5`. An `%error` checks the stride. `.text` **-6**.
3. **`mega_capon`** takes `[mega_capsw]` as a byte through `push ax`/`pop
   bp` (AH is 0 there) instead of `mov bp, [..]` / `and bp, 0xFF`. `.text`
   **-3**.
4. **`mega_svc_lo`'s service/IRQ choice is one AX**: `mov ax, 0xFB01`, then
   `mov ax, 0x9000` on an IRQ, and one store each to `[mega_capsw]` and
   `.stiop`, in place of four byte stores and two copies of `call
   mega_capon`. `.text` **-12**. It is fewer instructions on both paths.
   The MEGA_DIAG arm keeps `mega_pmpre` on the IRQ path only (`or al, al`
   after `mega_capon`, which keeps AL).
5. **`mega_svc_lo.rm`: `lidt [mega_rmidtr]`** with no `cs:` override.
   Both ways in, DS addresses the kernel. On the 386 way it is KDATA's
   cached base, 0x600: clearing PE keeps the descriptor cache. On
   `mega_r286`'s way it is KERNEL_SEG. `.text` **-1**.
6. **The 286 reset path drops two ES saves the reset discards**
   (`mega_svc_lo.r286` and `mega_r286`), and loads the BDA with a 286
   `push imm`. `.r286` saved SS:SP *before* its `push es`, so the reset
   restores the stack to below the push and the `pop es` before the triple
   fault restored a register the reset then erases. In `mega_r286`, ES is
   the reset's undefined value either way, and nothing after the probe reads
   it: `.go` pops the frame's ES, and `fsx_rland` and `mega_dropr` load their
   own. `mega_fdrprobe` preserves every register. `.text` **-6**.
   *Equivalence by reading* (it is the 286 reset path); see ROWS for the
   86Box check.
7. **`mega_pic_at`**: `mega_pic_pm` now loads `mov ah, MEGA_PIC_BASE` and
   falls into a body that takes the base in AH. This is msp1/shared's
   CROSS-FILE request, so that `megados.inc`'s `mega_dropr` can share the
   ICW1-ICW4 body (CROSS-FILE). `.text` **+2** until megados takes it.
   `mega_pic_pm` now returns AH = 20h. Its three callers were read
   (`mega_r286`; `mega_pic_pmf` from `mega_pmtail`, from megados's resume
   and from the hibernate wake), and none holds AH across it.
8. **`mega_svc_back` restores ES and DS in one loop** over `+0`/`+2` of the
   frame and of `mega_ses`/`mega_sds` against `mega_res`/`mega_rds` (an
   `%error` checks that layout). `.mega` **-14**. Cycles: about +20 per
   reflection for the loop overhead. A reflection is a mode switch, and a
   CPU reset on a 286, so this is noise. Reflection is not on the brief's
   hot list.
9. **`mega_k09`'s Ctrl-Alt-Del takes `mega_svc`'s warm-boot tail**
   (`.warm`: 1234h at 40:72, the 8042's reset line, `hlt`) by `push ds /
   pop es / jmp`, entering below the int 19h arm's CMOS clear, which the
   key never did. `.mega` **-12**. Off the per-key path: it is reached only
   on the reset chord.
10. **`mega_i16`'s dispatch folds 00h-02h with their 10h-12h twins**
    (`and bl, 0xEF`; only those six functions land on 0-2), then `cmp ah,
    5` for 05h alone, so 15h is still no function. `.mega` **-13**.
    Cycles, 286, to reach the arm: peek (01h/11h) 15 against 22/28,
    shift 02h/12h about equal, read 00h 18 against 10, read 10h 18 against
    16. A read that finds a key is therefore 2-8 cycles slower, and a read
    that waits blocks on `hlt`. int 16h is not on the brief's hot list. The
    peek, which is what a polling program loops on, is faster.
11. **`mega_slidec` and `mega_oddsegc` aim their scratch through the
    existing routine**: `mega_slidec` through `mega_gdtaim`, and
    `mega_oddsegc` through `mega_kaimf` with DI = ODD, exactly as
    `mega_sldsegf` already does with SLD. Each was a hand-written copy of the
    same eight-byte descriptor store under `pushf`/`cli`. `mega_slidec`'s
    own ES save goes with it (`mega_selbase` and `mega_gdtaim` keep ES).
    `.cold` **-71**. Cycles: one more near call and far call per slide or
    odd segment. Those run per 32/64KB of an LZ stream or per load, and
    neither is on the hot list.
12. **`mega_idt_gate` / `mega_irqrom`**: LOW0 through a 286 `push imm`
    instead of `push ax / mov ax / mov es / pop ax`. The gate is written
    with `xchg ax, dx` / `xchg ax, cx` (CX and DX are documented clobbers,
    and the two callers that loop count in BX) and `cbw` for the zero
    word. The IRQ9 redirect test compares memory. `.cold` **-6**.

Hot paths: `mega_t08`, `mega_k09`'s per-key path, `mega_gpfix`, `mega_gpw`,
`mega_null0`, `mega_dsimg`/`mega_csdata` and the `mega_pcin`/`pcout` catch
are **untouched**. `hotpar` (fast tier) is green. The `.text` changes are
all inside the reflection's window and the 286 path, ahead of every
kern_big hot routine `hotpar` names.

## REFUSED

* **`mega_k09`'s F1-F10 / F11-F12 modifier ladders as one body with the
  steps in registers** (est. -22 `.mega`). Built on paper: it costs every
  function-key press two `mov reg, imm` (~5 cycles on IRQ1), and merging
  only the two `xor al,al / jmp .store` tails costs a jump on the unmodified
  F11/F12. Neither is cycle-neutral on IRQ1, so both are refused under the
  hot-path rule. They are the only shape that pays there.
* **`mega_dsimg`'s kernel-CS arm moved behind the pair arm** (the pair path
  falls through, -0 bytes, about -4 cycles on a module's CSDO, +11 on the
  kernel's own CS). Refused: it is not neutral on both callers, and modcsdo is
  changing how many sites call it.
* **`mega_kwd` reading `[ss:mega_kwdt]` instead of loading ES**: SS is
  LOW_SEG on the shipped kernel's private chain stack, but not under
  `NOCHAINPRIV`, so it is not provable.

## DEFERRED (estimates, labelled)

| est. bytes | where | what it would take |
|---:|---|---|
| -6 `.text` each (assembled, pass 1) | `mega_slidef`, `mega_oddsegf`, `mega_s2segf`, `mega_irqromf` doors | every caller names the `.cold` body (`COLD_SEG:mega_*c`): compress, diskops, diskw, driver, hiber, loader, lz, kernel.asm, fsx (shared's), megados (`mega_irqromf`), pmfd's `mega_dosregion`. Then the door goes, and with it ~50 cycles a call |
| -9 `.text` net | `mega_pic_at` | megados's `mega_dropr` takes it (CROSS-FILE); until then item 7 is +2 |
| ~-12 `.mega` | `mega_report` | the four `mov di / mov ax,[ss:bp+n] / call mega_bhex4` after the vector as a 4-entry table of (gap, frame offset). The fbuf fields are 5, 4, 1, 4 bytes apart. The obstacle is that `[bp+bx]` is not an 8086 address, so the loop wants a spare index register; fault path, cold |
| ~-20 `.mega` | `mega_svc` front half | ES and DS to segments as one loop like item 8. The ES arm carries the above-1MB bounce test in the middle, so the loop needs a skip on the DS pass |
| ~-15 `.text` | `mega_capon .pair` | save the twelve cells first with two `rep movsw` (DS = 0, ES = KERNEL_SEG) and drop the per-cell stores from the filtering loop. About break-even on paper once DS is swapped and restored, so measure |
| ~-1 `.mega` | `mega_tell` | the shipped arm's `call mega_fkdo / ret` as a tail jump (MEGA_DIAG keeps the call) |
| ~-40 `.mega` | `mega_gpfix` / `mega_gpw` prefix walkers | from pass 1: one walker for both, cycle-neutral only (MEGATRAP's census is why) |
| ~84 `.text` | `mega_dsimg`, `mega_csdata` | from pass 1: PM-only after boot, but CSDO's per-instruction door. Revisit after modcsdo's pass, which changes how many sites call them |
| ~112 `.text` (file only) | `mega_capsave` (48), `mega_ivtsh` (64), other `dw 0` | zero-initialised data in `.text`: moving it to `.bss` shrinks the disk file, not the resident image |
| ~48 `.text` | `mega_bgdt` + `mega_bwin` | bootonce's this round (brief 0.2) |

## WANTS THE OWNER

Nothing user-visible changed. One behaviour is now SHARED rather than
copied: Ctrl-Alt-Del in a protected-mode program runs exactly the
instructions it ran before, at a different address.

## WANTS AN ABI BREAK

* **Retire the #GP fixups for packages** (`mega_gpfix`, `mega_gpw`,
  `mega_null0`/`mega_db`, `mega_strcal`/`mega_strfix` and `mega_tramp`).
  Today a package that writes through CS, through segment 0, or far-jumps
  into its data segment is completed by a fault apiece. A package-format rule
  that a kern_mega-built package does not do any of these would let
  `PKG_FMT` refuse an older image on kern_mega. Est. **~1,400 `.mega` + ~60
  `.text`** (the trampoline and the string-restart state), from the source
  span `mega_gpfix`..`mega_strfix`; this is an estimate, and the 286
  string-restart calibration goes with it. Breaks every package built
  before the flag day, on kern_mega only. Drivers have the same habit
  (`HDDTOOL`'s code-alias `rep movsb`, KERN-MEGA-PLAN 10.3 item 54.1), so
  `DRV_VER` too. It cleans up a contract that is enforced today only by
  faulting.

## CROSS-FILE

* **megados** (`megados.inc`, `mega_dropr`): replace its `mov al,0x11 / out
  0x20 / mov al,0x08 / out 0x21 / mov al,4 / out 0x21 / mov al,1 / out 0x21`
  (16 bytes) with `mov ah, 0x08 / call mega_pic_at` (5). `mega_pic_at`
  writes back the IMR it read, which `mega_dropr` had just set to FFh, and
  then `mega_dropr` writes its own masks, so the result is unchanged. Est.
  **-11 `.text`**, -9 net with item 7.
* **megados** (`megados.inc:1823`): `call KERNEL_SEG:mega_irqromf` ->
  `call COLD_SEG:mega_irqromc`, and the door goes (-6 `.text`). This is
  pass 1's item, still open.
* **shared** / **pmfd**: callers of `KERNEL_SEG:mega_slidef` /
  `mega_oddsegf` / `mega_s2segf` name `COLD_SEG:mega_slidec` /
  `mega_oddsegc` / `mega_s2segc` (-6 `.text` per door once its last caller
  moves). This is pass 1's item, still open.
* **all**: `mega_oddsegc` now reaches `mega_kaimf`. That is a far call
  inside `.cold`, which `mega_sldsegf` already makes. It changes no contract
  (FLAGS and every register but AX kept).

## ROWS TO RUN (named, not run)

Bochs (`tests/suite.py` / `tests/megaport.txt`):

* `megahddtick` and `megavidtier`: reflected int 13h and int 10h through
  `mega_svc` / `mega_svc_lo` / `mega_svc_back` (items 1-5, 8).
* `megapmmouse`: the int 33h capture and `mega_m33go` (items 1-3).
* `megafdnat`: the native floppy and its reflected fallback (`mega_svc`,
  `mega_svc_back`).
* `megapiclose`: the 8259 re-init (`mega_pic_pm` in its new shape, item 7).
* `megaflt`, `megainst`: the fault path, which was not changed, but shares
  `mega_svc`'s section with the new `.warm` label.
* `megapaint`, `megamod`, `megapkg`, `megahuge`, `megarehome`: LZ loads
  through `mega_slidec`, `mega_oddsegc`, `mega_backf` and `mega_kaimf`
  (item 11), and an IDT rebuild through `mega_ivt_sync` (item 12).
* `megakbtrip`, `megacaps`, and any `mega-*` port that types into a package
  (`mega-uilayer`): `mega_k09` and `mega_i16` (items 9-10).

86Box, which the coordinator runs and I did not launch:

* **`286-mega`**: boot to a desktop, open a package from B: (each reflected
  int 13h goes through `mega_svc_lo.r286` and `mega_r286`, items 4-7), run a
  DOS program through a live resume (`mega_r286` -> `mega_rland`), and
  check that `faults` is empty.
* **`286-mega`, Ctrl-Alt-Del** at the desktop: a warm reboot (item 9). No
  row asserts this today, so this is the check by eye.
* **`vm/at286-picomem1`** (tools/os88box.py) with Tracker playing: an
  option ROM's IRQ reflected through `mega_capon` / `mega_capoff` / the
  `mega_cap2` stubs, on the 286 reset path (items 1-4).

## DEFECTS FOUND

None in behaviour. Base `make` failed `checkdocs` because the brief's commit
left `docs/INDEX.md` stale; it is regenerated in this branch's first commit.
