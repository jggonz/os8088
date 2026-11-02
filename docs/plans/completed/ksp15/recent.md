# ksp15 agent "recent" - the unpassed kernel work, the relocation rows, kern_mega's first pass

Kernel size pass 15 (`docs/plans/completed/ksp15/BRIEF.md`, row `recent`).
Three pieces, in the brief's order: the kern_big/kern_small hunks committed
since pass 14 (`a9611731..e4896481`: `ico_grow`, `osapi_seg`, `mem_rr_ivt`,
`asc_use_x`, `ico_add`/`ico_need`); ksp14 `nopin.md`'s CROSS-FILE item, every
kernel-tag relocation proc of the bare `mov [w], dx / ret` shape as a
`mem_rr_tab` row; and a first pass on kern_mega, which had never had one.
Branch `ksp15-recent`, base `49ad5d67` (the brief's commit on `e4896481`).
Figures are ASSEMBLED bytes, `tools/kernsize.py --json` absolutes (kern_mega
from its own map); resident = `.text` + `.bss` + `.cold` + `.lowbss` +
`.vgabuf`, plus `.mega` on kern_mega. Clocks are 8088 unless a line says 286.

## BASE AND TIP

| | base | tip | delta |
|---|---:|---:|---:|
| **kern_big resident** | 90,008 | **89,972** | **-36** |
| kern_big `.text` / `.bss` / `.cold` / `.lowbss` / `.vgabuf` | 41,136 / 5,398 / 37,780 / 5,358 / 336 | 41,124 / 5,392 / 37,762 / 5,358 / 336 | -12 / -6 / -18 / 0 / 0 |
| kern_big `.boot2` / `.ovl` / `KERN_SIZE` | 6,447 / 5,110 / 97,280 | 6,447 / 5,110 / 97,280 | 0 |
| **kern_small resident** | 58,220 | **58,220** | **0** (`.text` 30,802 -> 30,806, `.bss` 2,969 -> 2,965: two words moved from `.bss` into `.text`) |
| kern_small `.boot2` / `.ovl` / `KERN_SIZE` | 4,565 / 1,482 / 59,904 | 4,565 / 1,482 / 59,904 | 0 |
| **kern_emu resident** | 90,264 | **90,228** | **-36** (`.text` 41,392 -> 41,380, `.bss` -6, `.cold` -18) |
| kern_emu `.boot2` / `KERN_SIZE` | 6,562 / 97,792 | 6,562 / 97,792 | 0 |
| **kern_mega resident** (with `.mega`) | 124,192 | **123,711** | **-481** |
| kern_mega `.text` / `.bss` / `.cold` / `.lowbss` / `.vgabuf` / `.mega` | 47,548 / 5,435 / 44,957 / 7,679 / 336 / 18,237 | 47,554 / 5,429 / 44,876 / 7,679 / 336 / 17,837 | +6 / -6 / -81 / 0 / 0 / -400 |
| kern_mega `.boot2` / `.ovl` | 6,620 / 5,109 | 6,620 / 5,109 | 0 |

kern_mega's segment (`.text` + `.bss`, 52,983) is unchanged; its resident
below 1MB after the switch (`.cold` and `.mega` go above it) is the same
too. What it gave back is `.cold` and `.mega`, the code above 1MB.

No rung moved on any kernel. `.boot2`, `.ovl` and `.lowbss` are untouched
everywhere, and so is every stack.

## TAKEN

### kern_big: the relocation rows - -18 on kern_big and kern_emu (`.text` -12, `.bss` -6)

pass 14 had already made `[menu_sseg]`, `[ico_seg]`, `[dsk_rah_seg]` and
`[asc_seg]` rows (`MEM_RR_ONE`, four bytes each) and left `clip_reloc` and
`sc_reloc` as procs (five bytes each, `mov [w], dx / ret`). A row is four
bytes whether it names one word or a run of adjacent ones, so:

* **`[ico_seg]` and `[dsk_rah_seg]` are adjacent in `disk.inc` and one row**
  (`MEM_RR_ROW ico_seg, 2, 2`, asserted where they are declared).
  `ico_seg` moved beside `dsk_rah_seg` and not the other way round:
  kerndos assembles `disk.inc` and needs `dsk_rah_seg` (`kdshim.inc`), and
  the icon store is under `%ifndef KD_BUILD` there, so `ico_seg` is too.
* **`[menu_sseg]`, `[asc_seg]`, `[clip_seg]` and `[sc_seg]` are one block,
  `mem_rr_lone` in `memory.inc`, and one row** - declared in `.text` with
  real initialisers, outside every `OS88_COMPACT` span (every kernel has the
  words; kern_big walks them). Their declarations in `menu.inc`,
  `assoc.inc`, `clip.inc` and `desksc.inc` say where they went.
* **`clip_reloc` and `sc_reloc` are `mem_rr_none` off kern_mega** (kern_mega
  keeps both procs, as it kept the other four: its words can hold a
  selector, which a row must not compare). The free/shed reading the
  caveat asks for:
  * `[clip_seg]`: set only from `clip_put`'s own claim (`mov [clip_seg], dx`
    right after `mem_claim`); zeroed by `clip_drop` with nothing that can
    compact between its `mem_free` and the store; `MEM_K_CLIP` is not
    purgeable, so no shed reaches it. The clipboard holds one claim at a time.
  * `[sc_seg]`: set from the claim (`ovl_sc_load`, `sc_m_kb`), zeroed by
    `ovl_sc_free` and by `sc_m_kb`'s free arm, in each case after the
    `mem_free`/`mem_regrow` returns with nothing between; `MEM_K_SC` is not
    purgeable. A regrow that relocates moves the claim through the
    compactor, which now rewrites the word through the row instead of the
    proc - and `sc_m_kb` then stores the same DX.
  So neither word ever holds a base that is not its own live claim, which is
  what makes a row exact.

Four rows x 4 + two procs x 5 = 26 bytes became two rows x 4 = 8; three
`.bss` words became `.text` words (+6 `.text`, -6 `.bss`). **A PARITY PAD
was added** (`clip.inc`, one never-executed `db 0` where `clip_reloc` was): the
five bytes left `ui_task`, the one hot `.text` routine after it, at an odd
shift. `ico_reloc`'s own pad went (`sc_reloc`'s odd five after it evens
`.text` out again), so the net is the same pad count. `mem_rr_walk` is faster
for every block the compactor moves: four fewer rows to load (~70 clocks
each).

`tests/megaseg.txt` +18 (4,417 -> 4,435): kern_mega's segment is the same
52,983, kern_big's shrank by rows kern_mega does not walk, and the file
measures the difference.

### kern_big: `asc_use_x` - -6 (`.cold`)

The claim sized from the file (SPEC.md 54.7.7, #318) banked the cluster and
the sectors on the stack across `mem_claim`/`mem_movable`, needed a
three-instruction `.noclaim` to unwind them, and pushed DX again around
`dsk_read_chain`. Now SI holds the cluster and CX the sectors (`mem_claim`
keeps every register but DX, `mem_movable` all of them; the KENT banks both),
so a refusal is a plain `jc .rfno`; `dsk_read_chain` keeps DX by its contract,
so its push/pop goes; and ES is loaded from DX before `mem_movable` rather
than from `[asc_seg]` after it (the same value).

### kern_big: `ico_grow` at its one call site - -7 (`.cold`)

`ico_grow` (63 bytes, new in #318) had one caller, `ico_add`. Written there it
banks BX and DX (the body's inputs) and keeps the row in CX, which `ico_add`
already banks; `cbw` replaces `xor ah, ah` (AL <= `ICO_KBMAX` = 20) and
`xchg ah, al` replaces `mov ah, al / xor al, al` (AH is 0 from the `cbw`).
On a refusal AX comes back as it went in, as before. SPEC.md 25.9.6 says
where the grow lives now.

### kern_big: `ico_need` - -5 (`.cold`)

`mov bx, MEM_P_ICO` before `mem_movable` reloaded the tag `mem_claim` had just
kept in BX, and `push dx / pop dx` banked a DX `mem_movable` does not touch.
(kern_small: inside `OS88_COMPACT`, so unchanged there.)

### kern_mega - `.mega` -400, `.cold` -63 (and `.cold` -18 from the kern_big items above)

Three commits, measured each: `5774e58f` `.mega` -305 / `.cold` -30,
`6f8ae982` -78 / -33, `12db79dc` -17 / 0. By kind:

**286 shifts and the push of a selector.**

Everything in `.mega` runs in protected mode, so on a 286, and `mega.inc` is
`cpu 286` throughout:

* thirty `mov cl, N / shift r, cl` pairs take the count as an immediate,
  and ten runs of `shift r, 1` became one shift; the `push cx / pop cx`
  pairs that only guarded CL went with them (`mega_dtopkb`, `mega_slotof`,
  `mega_xmsel`'s window KB, the park's three region sites);
* `push word SEL / pop es` for `push ax / mov ax, SEL / mov es, ax / pop ax`
  (ten LDT sites and `mega_gdtaim` in `.cold`, whose callers are all behind
  `[mega_pm]`; -3 each) and for `mov ax, SEL / mov es|ds, ax` where AX is
  dead (eleven sites, -1 each). Not in `mega_dwake`, whose stack on entry
  is the park stub's;
* `.cold`'s protected-mode-only sites (`mega_xmrunf`, `mega_layoutf`'s
  `.lin`, already 286 code) likewise.

Cycles: an immediate shift is 5 + n on a 286 against 2 a single-bit shift,
so the runs of two cost 3 clocks; none of them is in an ISR or `mega_gpfix`.

**Helpers for what was written out.**

* `mega_kblin` (KB to bytes) called at four sites that wrote its body and by
  `mx_kblin`; `mega_kbc`, `.cold`'s copy, for five protected-mode sites
  there, and `mega_mxbase` called where its body was.
* `mx_kb`, `mx_tokb`'s unchecked tail, replaces `mega_dosparkf`'s `.tokb`
  and one inline copy.
* `mega_p2lin` (a paragraph to its linear address) for nine `.mega` sites.
* The park's addresses: `mega_dpb`/`dpbx` (the park base plus an offset),
  `mega_dpdst`/`dpdstx` (the same into `[mega_xdst]`), `mega_dkblin` (a KB
  into the image past its header - `mega_dhsave`'s body, which falls into
  it), `mega_xsrc0`/`xsrcset` (`[mega_xsrc]`, twelve sites; the three kernel
  sources are one immediate, `(KERNEL_SEG << 4) + label`, which overflowing
  16 bits would be an assembly error), `mega_dlines` (KB to 64-byte lines).
  `mega_dosparkf` 1,118 -> 979, `mega_dstubup` 162 -> 105, `mega_dhcfill`
  156 -> 136, `mega_dosgo` 393 -> 366.
* `mega_sldsegf` is `mega_kaimf` with DI = `MEGA_SEL_SLD` (29 -> 10 bytes),
  by the far call `mega_vbaimf` already makes; 8086 code, as real mode
  reaches it on any CPU. +4 bytes of stack on that call.
* `mega_segf`'s verb dispatch takes two verbs a compare (`jb`/`je`), the
  verbs being 0..8 in order (asserted).

**Dead code** (-4).

`mega_fdgo` computed the drive's motor bit into AL (`mov al, 10h / shl al,
cl`) and then read port 21h into AL; the next instruction that reads flags
is three on. The 3Fh byte's drive bits take CH without a CX bank (the
routine's contract clobbers CX).

## REFUSED

1. **`osapi_seg`'s verb ladder one byte shorter** (-1, `.cold`): it is the
   routine in front of `gfx_blit1_x`, `dsk_xfer`, `dsk_runcap` and
   `dsk_next_clus_x`, and an odd change there puts all four at the wrong
   parity. `.cold` takes no data, so no pad; reverted. CROSS-FILE has the
   pairing.
2. **`ico_seg`/`dsk_rah_seg` into `mem_rr_lone`** (-4 more): kerndos
   assembles `disk.inc` and not `memory.inc`.
3. **`pusha`/`popa` in `.mega`** (~-10 a routine: `mega_mpmovef`,
   `mega_vxpm`, `mega_mods_fix`, `mega_mpsyncf` and some twenty more with
   four to seven pushes): two to six bytes deeper on stacks nobody has
   measured on kern_mega (the compactor's path is the UI task's).
4. **`mem_hitag` as range compares or a table**: 73 bytes against 64 as
   code, and a table would go in kern_mega's `.text`, the segment
   `tests/megaseg.txt` ratchets.
5. **`osapi_img_name`'s `.mod`/`.drv` arms as a `db 3Dh` skip** (-1): data in
   `.cold`, which `os88ovlchk` refuses (ksp11 multisel, same reason).
6. **`img_reloc` as a row**: pass 14's refusal stands (a stale `IMG_DVSEG`
   in a reused label row).
7. **`mega_dropr`'s `mov cl, 6 / shl ax, cl` as an immediate** (real mode,
   on the 286 the park ran on): kern_mega's `.text` is padded even, so the
   byte vanished into alignment and only moved every later `.text` routine
   by one.

## WANTS THE OWNER

Nothing. Every item is byte-identical behaviour.

## WANTS AN ABI BREAK

Nothing.

## CROSS-FILE

* **`fdlg_rqsp` (`fdlg.inc`) and `drv_blkseg` (`driver.inc`) can join
  `mem_rr_lone`** for -4 each: both are rows already (so exact by pass 14's
  reading) and neither file is in kerndos. `dsk_dseg` and `dskw_czseg` cannot
  (kerndos assembles `disk.inc` and `diskw.inc`) unless each gets a
  neighbour row in its own file.
* **The `clip.inc` parity pad** (+1 `.text`) goes with any odd `.text`
  change between `clip.inc` and `ui_task` (`instance.inc`, `menu.inc`,
  `fprog.inc`, `toast.inc` - gui's).
* **`osapi_seg` -1** (REFUSED 1) pays with any odd `.cold` change between
  `osapi_seg` and `gfx_blit1_x` (`osapi_img_name`) and another between
  `gfx_blit1_x` and `dsk_xfer`.
* kern_mega: `mega_slidef`'s `.desc` has `push ax / mov ax, MEGA_SEL_WIN /
  mov es, ax / pop ax` in `.text` (-3, odd) and `mega_k09`, `mega_gpfix`,
  `mega_gpw`, `mega_fkill`, `mega_fd13` (the five largest `.mega` routines
  left, 434-610 bytes) have not been read for structure.

## ROWS RUN

Once, after the last code commit, `os88test.py soak -k ... --marty-jobs 1`
(one emulator at a time), plus `full -k pins`:

* **kern_big, 23 of 23 green**: `sndmove`, `drvmove`, `regapp`, `regwork`,
  `regpin`, `regmove`, `heapcheck`, `tnest`, `pins`, `pinsrt` (the rows and
  the compactor's walk), `clipkeep`, `atclip` (`[clip_seg]` as a row),
  `desksc`, `stikmove` (`[sc_seg]`), `dirwshed` (`[dsk_rah_seg]`),
  `icostore`, `icogrow` (the inlined grow), `icoshed`, `icohd`, `ascabsorb`,
  `assocopen`, `assocflush`, `assocvol` (`asc_use_x`).
* **kern_mega, 17 of 18 green under Bochs** (Bochs installed for this, `apt`,
  docs/BOCHS.md): `megadosp`, `megadosroom`, `megahiber`, `megahibfull`,
  `megap2hiber`, `megahma` (the park, its helpers, the HMA save),
  `megahuge`, `megaxmove`, `megaown`, `megacaps` (the pool, `mx_*`,
  `mega_hugef`), `megasweep`, `megatbl` (`mega_layoutf`), `megashed`,
  `megainst`, `megarehome`, `megaovl`, `megafont`.
  **`megamod` FAILS AT THE BASE TOO**, the same way: a #GP at `1154:089A`
  (and `087F`, a #UD at `1154:0008`) in an LDT code selector - every
  sound-playing leg, Tracker, ModPlug and MIDIRack alike - "while waiting
  for the module's open". Re-run alone at the tip (red), with only this
  branch's kern_big commit under kern_mega (red), and with `kernel/` at
  `49ad5d67` (red, identical frames). Not this branch's; not chased.
* `megacz` SKIPs: it needs 86Box, which this container does not have built.
  `mega_sldsegf` (the LZ decoder's input selector on the segment arm) is
  reached by every packed file kern_mega reads, so every Bochs row above
  ran it.

`make`, `make small`, `make emu` and `make mega` build at the tip; the fast
tier is green (`hotpar` with the pad above); `tools/stkbalance.py` over every
file touched reads the same unbalanced paths as the base.

## DEFECTS FOUND

None in behaviour. `mega_fdgo`'s dead motor-bit computation (TAKEN) reads
like a store that was meant for port 3F2h and lost to a later rewrite; the
port write below builds its own value from the 3Fh byte, so nothing is
missing.
