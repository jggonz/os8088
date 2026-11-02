# kern_mega size pass 1: pmcore

Owner of `kernel/mega.inc` lines 1-4968 at the base (`4fb1e6d5`): the switch,
the tables, the ROM halves, reflection, `mega_tell`, the fault reporter and
the #GP fixups. Branch `msp1-pmcore`. The brief is
[BRIEF.md](BRIEF.md).

The pass's one large finding is that **kern_mega carried boot-once and
protected-mode-only code in `.text`** - conventional memory for ever - when
`.cold` is reachable from the switch's first real table on and its rung goes
to the heap at the switch. Moving it is where nearly all of this note's bytes
are.

## BASE AND TIP

`tools/kernsize.py --build build/megak --json`, base `4fb1e6d5`, tip the
branch head.

| kern_mega | base | tip | delta |
|---|---:|---:|---:|
| `.text` | 47,548 | 46,596 | **-952** |
| `.bss` | 5,435 | 5,435 | 0 |
| `.cold` | 44,957 | 45,936 | +979 |
| `.lowbss` | 7,679 | 7,679 | 0 |
| `.vgabuf` | 336 | 336 | 0 |
| `.mega` | 18,237 | 18,152 | **-85** |
| `.mega` scratch | 1,493 | 1,498 | +5 (rounding of the rung, not code) |
| `.boot2` / `.ovl` | 6,620 / 5,109 | 6,620 / 5,109 | 0 |
| `KERN_SIZE` at boot | 131,584 | 131,584 | 0 |
| resident below 1MB after the switch (`kernsize` footprint line) | 66,560 | 65,536 | **-1,024** (the image rung, 53,248 -> 52,224) |
| `.text`+`.bss` segment used, of 65,536 | 52,983 | 52,031 | **-952** |

So **952 more bytes of the 64KB window kern_mega shares with kern_big's
growth** (12,553 left at the base, 13,505 at the tip). Net bytes across all sections: `.text`+`.cold`+`.mega` =
-58.

The cost of the move: `.cold` crossed a 1KB granule, so `MEGA_CODE_KB` (the
code area above 1MB) is 65 KB where it was 64 - one KB of extended memory.

**kern_big: byte-identical** apart from the two build-number bytes every
commit moves (SPEC.md 14.2): every kernel source change is in `mega.inc`,
which is `%ifdef KERN_MEGA` from line 63 to its end; `cmp -l` against the
base's `kernel.bin` shows exactly the two build-number bytes. kern_small and
kern_emu assemble no line of it.

## TAKEN

1. **`mega_enter`'s body into `.cold`** (`mega_enterf`). It runs once, at
   boot. `.text` keeps a 16-byte `mega_enter` (far call, `jc`, `call
   mega_coldup`, far call `mega_pmtail`, `ret`); `mega_coldup` stays called
   from `.text` because it moves `.cold`. The bootstrap GDT's code entry
   `MEGA_SEL_BCODE` is now based at `COLD_SEG` rather than `KERNEL_SEG`, and
   the jump into the real tables names `COLD_SEG` (an identity code entry,
   as `KERNEL_SEG`'s is); the far return to `mega_enter` pops `0x0060`,
   which in protected mode is GDT entry 12, the kernel's identity code
   selector. The two `db 0xEA` far jumps became `jmp SEG:OFF` instructions
   (no data in `.cold`, SPEC.md 2.6 rule 1) - same encoding, checked with
   ndisasm. `.text` -370, `.cold` +381 (`clc`/`retf` and the far entry).
   *Equivalence by reading*: the body is the old one byte for byte but for
   the two far-jump targets, `retf` for `ret`, and `clc` before the success
   `retf` (the old routine's CF was not read: the refusal is now signalled
   by CF, which `mega_gatef`'s refusal already set). Nothing loads selector
   28h after the boot: the real GDT's copy of entry 5 is banked and put back
   by the DOS trip (`mega_dsave`), never loaded - grep of `kernel/`,
   `kerndos/`, `drivers/hiber` for `MEGA_SEL_BCODE`/`0x28` (megapark's 28h is
   the park stub's OWN table). Not on the 286 reset path.
2. **The IDT build into `.cold`**: `mega_idt_build`, `mega_idt_gate`,
   `mega_irqrom` (365 bytes of `.text`). Every caller is in protected mode
   (`mega_enterf` after the real tables, `mega_ivt_sync`, `mega_m33go`,
   megados's `mega_drom_scan`), where `.cold` is reachable at its identity
   before `mega_coldup` and above 1MB after. `mega_idt_gatef` is now
   `.cold`'s far entry (the two `.mega` callers name `COLD_SEG`, same cost
   as the `KERNEL_SEG` door they used); `mega_irqromf` stays a 6-byte `.text`
   door because megados.inc names it (CROSS-FILE).
3. **`mega_slidef`, `mega_oddsegf`, `mega_s2seg` into `.cold`** behind
   6-byte `.text` doors of the same names (`call COLD_SEG:...c / retf`), for
   their callers in compress/diskops/diskw/driver/hiber/loader/lz/kernel.asm
   and pmfd's range. Their real-mode test was `cmp word [cs:k_dsel],
   KERNEL_SEG`, which `.cold` cannot address; it is now `cmp byte
   [ss:mega_pm], 0`, the test their `.cold` siblings (`mega_backf`,
   `mega_sldsegf`, `mega_kaimf`) already make. *Equivalence*: the two flags
   agree everywhere but inside a reflection's real-mode window, where
   `[k_dsel]` is put back to `KERNEL_SEG`; none of the three runs there (only
   the BIOS and the mouse ISR do, and neither decompresses or converts a
   selector), and `.cold` is not reachable in that window anyway once it is
   above 1MB. `mega_svc`'s two calls and `mega_ivt_sync`'s call `COLD_SEG:
   mega_s2segc` directly, so the reflection path pays nothing; the door
   costs one far call + `retf` (~50 cycles on a 286) and 4 bytes of stack on
   the other callers, all per 32KB/64KB of LZ stream or per load. `.text`
   -86, -61 and -68 (net of the doors).
4. **`mega_ss` stubs 8 -> 5 bytes, `mega_xs` 8 -> 7.** The three `nop`s of
   padding went; `mega_idt_gate` computes `5v + (mega_ss - 50h)` and `8v -
   v + mega_xs` (it was `(v-10h)*8` and `8v`). `.mega` -64, `.cold` +1 net.
   Verified by a script over the built image: all 32 gates' targets decode
   to `push byte v / jmp near mega_svc` (or `mega_fault`, with the 0 pushed
   where the CPU pushes no error code).
5. **`mega_i16`**: one `mov si, ax` before the dispatch instead of one per
   reader, and the two readers share the E0 tail (`.get` jumps to it; it
   blocks on `hlt` anyway, `.peek` falls in). `.mega` -15.
6. **`mega_nmi`**: `[mega_nmi61]` was written and immediately read back on
   the shipped build; it is `mega_nmitell`'s (MEGADIAG) alone. `.mega` -6.
7. One dead `mov cx, MEGA_SEL_CODE` in `mega_idt_gate`'s int 16h arm.
   `.cold` -3.

No hot path changed: `mega_t08`, `mega_k09`, `mega_gpfix`, `mega_gpw`,
`mega_null0` are untouched, and every kern_big hot routine `hotpar` names
sits in the final, EVEN, `.text` shift run (-952) on kern_mega too
(`os88rdiff`); the odd-shift runs are `mega_capon..mega_slidef` (the
reflection's real-mode window and its neighbours) and `.cold` from
`mega_hugef` on.

## REFUSED / WITHDRAWN

* **Gating three E9-only lines behind MEGADIAG** (`mega_mhd13` with its test
  on every reflected int 13h, `mega_strcal`'s `string restart`, `mega_kwd`'s
  `8042 edge lost` behind MEGADIAG-or-PICLOSE): built and committed
  (`.mega` -239), then **reverted** on the coordinator's word - the owner
  gates ALL of kern_mega's E9 output in one separate step. So was a shared
  loop for `mega_report`'s two E9 word dumps (-8). Both are in E9 SITES
  below.
* A `jmp far` door with an unreachable `retf` after it (zero stack, fewer
  cycles than `call`/`retf`, same 6 bytes): refused by `t_asmrules`'
  unreachable-code rule, and a bare `jmp far` door by `os88ovlchk`'s
  far-tail-call rule. `call`/`retf` it is.

## DEFERRED (estimates unless marked)

| est. bytes | where | what it would take |
|---:|---|---|
| -6 `.text` (assembled) | `mega_irqromf` door | shared: megados.inc:1864 names `COLD_SEG:mega_irqromc` and the door goes (CROSS-FILE) |
| -6 `.text` each | `mega_slidef`/`mega_oddsegf`/`mega_s2segf` doors | every caller names `COLD_SEG:mega_*c` (compress, diskops, diskw, driver, hiber, loader, lz, kernel.asm, pmfd's `mega_dosregion`) - saves the door and ~50 cycles a call |
| ~48 `.text` | `mega_bgdt` + `mega_bwin` | the bootstrap GDT is dead after the switch; as `.text` data it is resident for ever. Build it on the stack or in the tables block inside `mega_enterf`, with pmmem's `mega_tset` writing `mega_bwin` through the same place |
| ~84 `.text` | `mega_dsimg`, `mega_csdata` | PM-only after boot but reached by `CSDO`/`PUSH_CSDATA` in modules per instruction - a `.cold` body would cost a far hop on a path that may be warm; measure first |
| ~4 `.text` | `mega_dsimg`'s `.pair` | shared epilogue by `jmp` (+1 jump on the pair path, so a cycle question) |
| ~112 `.text` (move to `.bss`, no RAM gain) | `mega_capsave` (48), `mega_ivtsh` (64), many `dw 0` | zero-initialised data in `.text`: shrinks the FILE, not the resident image. Only worth it for disk |
| ~40 `.mega` | `mega_gpfix` / `mega_gpw` prefix decoders | two prefix walkers over the faulting instruction; one table-driven walker could serve both, but both are the per-fault hot path (MEGATRAP's census) - only cycle-neutral |
| ~20 `.mega` | `mega_k09` | the F1-F10 and F11/F12 modifier ladders (+19h/+23h/+2Dh vs +2/+4/+6) as one table; and the `.f1112o`/`.fout` twin tails. Hot (IRQ1): only cycle-neutral rearrangements |
| ~20 `.mega` | `mega_svc_back` | `.esnew/.dsnew` twin selector restores as one subroutine; per reflection, so +call cost |
| ~10 `.mega` | `mega_report` | the five `mov di,X / mov ax,[ss:bp+Y] / call mega_bhex*` as a table (fault path, cold) |
| ~8 `.mega` | `mega_ivt_sync.cmprow` | the DS/ES swap around `repe cmpsw` (four pushes/pops twice) |
| 1 `.bss` | `mega_hdsaid` (pmfd/pmmem range) | goes with the E9 gating step |
| ~370 `.cold`/`.mega` | `mega_frames_fix`, `mega_mods_fix`, `mega_ivt_sync`'s first build, `mega_strcal` | boot-once protected-mode code; there is no section above 1MB that is given back. A `.mega` "init" tail freed after `mega_pmtail` would make it a true reduction of the extended-memory code area |

## WANTS THE OWNER

Nothing user-visible changed. The E9 gating I had taken is withdrawn in
favour of the owner's own step (below).

## WANTS AN ABI BREAK

None.

## CROSS-FILE

* **pmmem**: lines `mega_irqromf:`/`mega_idt_gatef:` (`call ... / retf`, 4
  bytes each) in "THE DOORS BETWEEN `.text` AND `.mega`" were DELETED in this
  branch - unavoidably, as their targets left `.text`. A merge conflict there
  resolves to the deletion; `mega_irqromf` now lives in my range as a door to
  `.cold`, `mega_idt_gatef` is `.cold`'s.
* **shared**: megados.inc:1864 `call KERNEL_SEG:mega_irqromf` may become
  `call COLD_SEG:mega_irqromc` (then delete the door: -6 `.text`).
* **all**: callers of `KERNEL_SEG:mega_slidef`/`mega_oddsegf`/`mega_s2segf`
  may name the `.cold` bodies `COLD_SEG:mega_slidec`/`mega_oddsegc`/
  `mega_s2segc` directly (-6 `.text` per door once its last caller moves).
  pmfd's `mega.inc` ~5238 is one.

## E9 SITES (in my range, for the owner's gating step)

| routine | section | prints | string bytes |
|---|---|---|---:|
| `mega_kwd` `.lost` | `.mega` | `kern_mega: 8042 edge lost, 64=ss` (read by `tests/megapiclose.py` on PICLOSE=1) | 31 `mega_mkwd` |
| `mega_svc` `.refl1` | `.mega` | `kern_mega: a fixed-disk transfer went to the BIOS (int 13h reflected)`, once, plus the per-int-13h test and `[mega_hdsaid]` | 71 `mega_mhd13` |
| `mega_report` | `.mega` | `kern_mega FAULT vector vv  stack: ...` and `ES DS DI SI ... SS:` (read: megainst, megasheet, docs/BOCHS.md) | 25 + 10 + 37 |
| `mega_fkmsg_mk` | `.mega` | `kern_mega CLOSED NAME vv at cccc:iiii err eeee` | 19 + 6 |
| `mega_strcal` | `.mega` | `kern_mega: string restart r.... p....` | 28 + 3 |
| `mega_tlog` | `.mega` | MEGATRAP's `T` lines (already MEGATRAP-only) | - |
| `mega_nmitell` | `.mega` | the NMI lines (already MEGADIAG-only) | - |
| `mega_drvsayf` | `.mega` | `kern_mega: above 1MB: NAME` (read by many rows) | 23 `mega_mdrv` |
| strings for other ranges | `.mega` | `mega_mon` (27, pmmem's `mega_pmtail`, read by ~20 rows), `mega_mfdoff`/`mfdon`/`mfdfb` (57/26/50, pmfd's native floppy) | 160 |
| `MEGA_E9IO mega_` / `mm_` | `.text` / `.mega` | the two copies of say/puts/putc/hex4/hex2 (~45 bytes each) | - |

Gating the three no-reader lines (`mhd13`, `string restart`, `edge lost`)
measured **-239 `.mega`** on this tree before it was withdrawn.

## ROWS RUN

One `python3 tools/os88test.py soak -k ... --marty-jobs 1` pass at
`ca2de747` (Bochs; no code changed after it), chosen for what the moves reach:
every row boots through `mega_enterf` and the `.cold` IDT build; LZ package
loads go through `mega_slidef`/`mega_oddsegf`/`mega_s2seg`; reflections
through `mega_svc`'s direct `mega_s2segc`; the fault path through the `mega_xs`
stubs and `mega_report`/`mega_fkill`; IRQ gates through `mega_ivt_sync`.

| row | result |
|---|---|
| `megahddtick` | ok 16.3s |
| `megapaint` | ok 34.4s |
| `megavidtier` | ok 53.4s |
| `megaflt` | ok 52.6s (faults close packages: `mega_xs`, `mega_fkill`) |
| `megainst` | ok 81.4s (`kern_mega FAULT vector` path) |
| `megapmmouse` | ok 38.2s (`mega_m33go`'s `.cold` gate rebuild) |
| `megafont` | ok 84.6s |
| `megakbtrip` | FAIL 196.7s in the pass (trip 1: *the launch made twice, KBTRIP never started*); **GOOD 2/2** re-run alone at the same commit (`tools/os88bisect.py classify megakbtrip`: *"CONTENTION or an intermittent ... nothing to bisect"*). Its row note already records it as INTERMITTENT at base, and three other agents shared the 4 cores |

Not run: the 286 reset path (no emulator here runs it, docs/86BOX.md);
nothing on it was changed. The `mega-*` 86Box/Bochs ports in
`tests/megaport.txt` beyond these were not run: these eight cover every moved
routine.

## DEFECTS FOUND

None in behaviour. Stale comment fixed: `MEGA_SEL_BCODE`'s note said the ROM
halves are entered through it; they have been entered through
`MEGA_SEL_CODE` since `.mega` existed.
