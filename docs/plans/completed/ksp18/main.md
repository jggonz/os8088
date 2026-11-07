# ksp18 agent "main" - the work that arrived from main, and PCIETHER.DRV

Kernel size pass 18 (`docs/plans/completed/ksp18/BRIEF.md`, row `main`): every routine
`docs/plans/completed/ksp18/fresh-routines.md` assigns `main` - `f113f3f7` (`kbm_isr`'s
IRQ1-only ROM handoff, the splash's driver message in `.boot2`, MOUDIAG),
`d7ac8738` (WIN16 Phase B/C: `assoc_ownex`, `assoc_run_x`'s ASSOC_PWBANK
arm), `4faf9339` / `eb1caa77` (PCIETHER.DRV's kernel rows: `drv_f_pcieth`,
`drv_e9`, `bdt_`/`drvt_pcieth`, the row `vesa_row`'s span grew by),
`d401cf8c` and `a408aaf5` (kern_mega: `mega.inc`) - and then, reported apart
as system-disk bytes, PCIETHER.DRV (`drivers/pciether/`, never size-passed)
and the shared driver body `drivers/ether/ethdrv.inc`. Branch `ksp18-main`,
base `c5a7b0f9` (the brief, on `aede17b1`). Figures are ASSEMBLED bytes,
`tools/kernsize.py --json` absolutes; resident = `.text` + `.bss` + `.cold` +
`.lowbss` + `.vgabuf` + `.vga` (+ `.mega` on kern_mega).

**TAKEN outside the row**: none in the kernel. `kernel/mega.inc`'s new
routines are the row's (`d401cf8c`, `d7ac8738`); `tests/megaseg.txt` moved
because of item 1 (below). The driver work is the brief's own second half.

**What the row's kernel bytes were.** Of `main`'s +125 (fresh-routines.md),
+56/-60 is `ovl_spl_msg_drv` and the stub it replaced, both `.boot2` (not
resident); `drv_e9`, `drvt_pcieth`, `drv_memk`'s word, `cdl_att` and the rest
are CTRL.DRV's (`.modu`) or the blob's; `vesa_row`'s +16 is PCIETHER's
16-byte `drv_tab` row, which follows that label unlabelled, and is the
table's fixed shape. What was resident and movable: `kbm_isr` (+26),
`assoc_run_x` (+20), `assoc_ownex` (+20, two of them parity pads) and
`drv_f_pcieth` (+13).

## BASE AND TIP

| | base `c5a7b0f9` | tip | delta |
|---|---:|---:|---:|
| **kern_big resident** | 91,767 | **91,749** | **-18** |
| kern_big `.text` / `.bss` / `.cold` / `.lowbss` / `.vgabuf` / `.vga` | 40,633 / 5,376 / 38,034 / 5,358 / 336 / 2,030 | 40,621 / 5,376 / 38,028 / 5,358 / 336 / 2,030 | -12 / 0 / -6 / 0 / 0 / 0 |
| kern_big `.boot2` / `.ovl` / `KERN_SIZE` | 6,456 / 5,117 / 97,280 | unchanged | 0 |
| **kern_small resident** | 57,333 | **57,331** | **-2** (`.text`) |
| kern_small `KERN_SIZE` | 59,392 | unchanged | 0 |
| **kern_emu resident** | 92,011 | **91,993** | **-18** (`.text` -12, `.cold` -6) |
| **kern_mega resident** (incl. `.mega`) | 127,276 | **127,244** | **-32** (`.text` -10, `.cold` -6, `.mega` 23,072 -> 23,056) |
| kern_mega conventional (`MEGA_RESIDENT`) | 63,488 of 68,096 | **62,976** | **-512: the image rung, with ONE byte left in it** |
| kern_mega `KERN_SIZE` at boot | 135,168 | **134,144** | -1,024 (the image rung and the `.mega` rung, 10 bytes left) |
| **PCIETHER.DRV** assembled (`build/pciether.bin`) | 17,265 | **17,057** | **-208** |
| PCIETHER.DRV packed file (`build/pciether.drv`) | 10,958 | **10,820** | **-138** |
| PCIETHER.DRV unpacked image (`os88drv.image_unwrap`) | 16,897 | 16,897 | 0 - see below |
| **ETHER.DRV** assembled (`build/ether.bin`) | 17,397 | **17,383** | **-14** |
| ETHER.DRV packed file | 11,069 | **11,066** | -3 |
| ETHER.DRV unpacked image | 16,901 | 16,903 | +2 - see below |

**The unpacked images do not move, and that is `os88drv.py`'s strip rule,
not the code**: the shipped image keeps its trailing zeros down to one byte
past the 512-byte rung below its claim (SPEC.md 51.1.2), so both drivers
stay at 16,897 + (0..15) inside the same 17,408-byte claim. The assembled
image is where the bytes are, and the packed file is what the disk carries;
ETHER.DRV's +2 is the paragraph rounding of that same strip moving with a
14-byte-shorter image. Neither claim crossed a rung - per CLAUDE.md's banner
that is not the question: PCIETHER.DRV is 208 bytes of code smaller, and
161 more would take its claim to 16,896.

**kern_mega's rung is on a knife edge.** The image rung reads "1 left": any
two bytes of conventional kern_mega growth put the 512 back. The coordinator
should know that before the merge reads this as a saving that will hold.

## TAKEN

**The speed rule** (BRIEF.md section 1): one hot routine touched - `kbm_isr`,
H2, made FASTER (item 1, counted). No khp1 pick and no KHP_* arm was
touched; nothing here is on the mount, listing, read-ahead, FAT-window or
loader path, so no operation gained or lost an `int 13h` call.

### 1. `kbm_isr`'s self-chain test against a constant (kern_big -2, small -2, emu -2, mega 0) - H2, FASTER

`mov ax, cs / cmp [cs:kbm_old9+2], ax` became `cmp word [kbm_old9+2],
KERNEL_SEG` on every real-mode kernel: DS = CS = KERNEL_SEG at `.chain`
(`KDS` at kbm_isr's head, and kbm_i15's head does the same), and KERNEL_SEG
is an `equ`. kern_mega keeps the live read - CS is a selector there.

Cycles, per IRQ1 that reaches the chain (every make and every break on an
XT), 8088 `max(clocks, 4.34 x bytes)`:

| | bytes | clocks | max |
|---|---:|---:|---:|
| `mov ax, cs` (8C C8) + `cmp [cs:m16], ax` (2E 39 06 m16) | 2 + 5 | 2 + (9 + 6 EA + 2 seg + 4 word) = 23 | **30.4** |
| `cmp word [m16], imm8` (83 3E m16 60) | 5 | 10 + 6 EA + 4 word = 20 | **21.7** |

~8 clocks a key event faster, nothing slower. AX was dead there on both arms:
the ROM block overwrites it with `in al, 0x21`, and the KBDDIAG int 15h arm
reloads the caller's AX from the frame (two comments that said "the CS the
self-chain test left in AX" now say the scan's). Parity: -2, `hotpar` green
on both gated kernels; `kbm_isr`'s entry is where it was. kern_mega's
`tests/megaseg.txt` 3,682 -> 3,684 (its kern_mega-only arm is the two bytes
kern_big no longer has; nothing of kern_mega's grew).

### 2. `drv_f_pcieth` is `'PCI'` in front of `drv_f_eth` (kern_big, emu, mega -10)

`'PCIETHER.DRV', 0` ends in `'ETHER.DRV', 0`, which is the very next line -
the shape DRV_TITLES' `'PCI '` + `'Ethernet'` already used. The row's
DRVR_FILE still names `drv_f_pcieth` (tests/pciether.py finds the row by
that address) and every reader of the name reads an ASCIIZ string.

### 3. `assoc_run_x`'s ASSOC_PWBANK arm and `assoc_ownex` (kern_big, emu, mega -6; kern_small has no assoc.inc)

* `.banked` reloaded `[assoc_dclus]` and `[assoc_ddrv]` only to have
  `.located` store them back where they came from - the one thing that
  store did beyond that was clearing `assoc_dead` (the word's high byte).
  Now `.banked` writes DI = 0 as a word at `assoc_dead` (its high byte is
  `[assoc_pwin]`'s low) and as a word at `assoc_pwin`, and skips `.located`'s
  stores: -3, the same five bytes of state on the way out. A `%if` pins the
  `assoc_dead`/`assoc_pwin` adjacency beside the existing one.
* `assoc_ownex` read the caller's banked slot through `mov bx, sp / cmp al,
  [ss:bx+2]`; the caller's `pop ax / push ax` (which it ran right after
  anyway) now runs before the call and the routine banks AL round
  `assoc_slot_of` and gives it back with `xchg ax, bx` (CF-neutral): -1.
* Its two parity pads went with those odd -3 and -1: -6 even, `hotpar`
  green (`dsk_xfer` and its .cold neighbours keep their parity).

H4 (once per document launch). Rows: `win16nedos` (leg D is the posted
`\AUTOSTRT` launch that keeps the locator), `win16nowin` (54.4.2.4, the
`assoc_ownex` refusal), the `assoc*` rows (`.nofile`'s rebuild and resweep).

### 4. Four `.mega` shapes in the new WIN16 code (kern_mega `.mega` -26 assembled, -16 after `align 16`)

* `mega_swc`: the stub's return address IS its vector byte, so `mov bx,
  [ss:bp+4] / sub bx, mega_swv / mov al, [cs:mega_swv+bx]` is `mov al,
  [cs:bx]` (spelt `[cs:mega_swv + bx - mega_swv]`, which assembles to the
  same `2E 8A 07`, because os88ovlchk wants a `.mega` label named in a CS
  read): -6.
* `mega_swc`'s `.sh` arm popped the gate `mega_idt_gatef` built (offset, then
  selector - the two words it read through `[ss:si]`/`[ss:si+2]`) and jumps
  back to the fast arm's one store pair and exit: -15.
* `mega_swok` and `mega_fattr` end on the compare's/callee's own CF (`pop`
  writes no flag): -2, -3.

`fsx.inc`'s `align 16` before `rms_lo` took the first 11 and gave back 16
when the rest crossed it, so the section reads -16. Protected-mode only, IF =
0 software-vector dispatch and the fault close's attribution - no hot path,
and not the KHP arms. No row here reaches them (see ROWS RUN): proved by
reading - each is an identity on the same memory (the gate's words are the
same two words; `[cs:mega_swv + (ret - mega_swv)]` is `[cs:ret]`), and flags
across `pop` are the 8086's rule.

### 5. PCIETHER.DRV: the first size pass (assembled -208, packed -138; no kernel byte)

* **`pci.inc`, rewritten round one helper.** `pci_bios` is the `int 1Ah`,
  then `neg ah` - CF = (AH != 0), every return code but SUCCESSFUL - and
  `pci_probe` makes the calls inline over it: the CPU gate, B101h and its
  `'PC'` signature, then per `pce_tab` row B102h, BAR0's read and its I/O
  bit, the Command register's read and its I/O Space | Bus Master write.
  Same calls, same order, same refusals and the same "a card we cannot drive
  is left as the BIOS set it" rule (SPEC.md 125.3). `pci_gate`,
  `pci_present`, `pci_find`, `pci_iobar0`, `pci_enable` and the never-called
  `pci_rdb`/`pci_wrb` doors are gone; `pci_probe` keeps DX (the attach's
  caller holds it) and spends CX and DI, which eth_attach banks. SPEC.md
  125.8 item 1's "`pci_gate`" now names `pci_probe`'s head.
* **`pce_find`** banked AX, BX, CX and DI that its one caller, eth_attach,
  already banks; the vector copy is indexed by BX; `[pce_bdf]` (written,
  never read anywhere in the tree) and a refused claim's `mov word
  [eth_base], 0` are gone - ethdrv.inc's own `.none` comment is the rule that
  a refused image is never read again.
* **`rtl8139.inc`.** `rt_port` takes the register's offset in AL (`cbw` is
  the zero AH: every RT_* is below 80h), so each of the eighteen call sites
  is a byte load; `rt_rstwait` is a `loopnz`; `rt_claim`, `rt_init`,
  `rt_start`, `rt_rxreset` (now a tail `jmp rt_start`), `rt_stop`,
  `rt_consume` and `rt_es` bank only what their one or two callers need kept
  - each routine's header names what it spends and who banks it, and
  ethdrv.inc's seam header now says nic_init may spend AX, BX, CX, SI and DI
  (keeping DX) and nic_stop anything. `rt_rx` and `rt_tx` - nic_rx and
  nic_tx, which the stack's STKDEPTH-NOSAVE markers lean on - keep every
  register they kept. `rt_rx` takes the frame's length from the register
  that was just tested against NIC_FRAME (-3); `rt_tx`'s runt pad is an
  indexed loop (-7).

The ring, the claim, the DMA-stop-before-free order, the dword door's `cli`,
the waits and every port write are unchanged in order and value. Stack: the
receive path is no deeper and the attach/detach paths are shallower
(`stkdepth --check` holds on both drivers).

### 6. `ethdrv.inc`, the body ETHER.DRV and PCIETHER.DRV share (both -14 assembled)

* `eth_name` and `eth_cpname` were two `'Ethernet', 0`; one string with two
  labels (-9). The guide page `ETHER_DRV` was restamped (its strings
  fingerprint counts the list; no string a user reads changed).
* `eth_attach`'s two exits are one: the found path's `mov si, eth_svc` is
  followed by `db 0xA8` (`test al, 0xF9`: CF = 0, the `stc` below its
  operand) into `.none`'s `stc` and pops (-5). AL on the refusal is still
  the kernel's DRVV_ATTACH from the frame, which drv_load_at reads as "no
  DRVE_* given" - exactly as before.

## REFUSED

| item | bytes | why not |
|---|---:|---|
| `kbm_isr`: keep DS the kernel's across the ROM call, so the ROM block falls into one `pop ds / pop ax / iret` that `.selfchain` jumps to | -1 (kern_small needing a pad, so 0 there) | the ROM would be entered with DS = KERNEL_SEG instead of the interrupted DS. Every ROM handler read loads its own DS first, so nothing should see it - but "should" on a ROM handoff the 5150 runs is a reading, not a proof, for one byte |
| `kbm_isr`: the `[kbm_ivec]` compare (per key on an XT, deciding the int 15h arm) | - | every cheaper shape found costs a second scan body or a stack marker the scan would have to carry; the test is 22 clocks and it is the cost of the int 15h door SPEC.md 9.7.2 chose |
| `ovl_spl_msg_drv`'s `' '` and `'('` as one two-byte string through `spl_mcs` | -2 `.boot2` | not resident, and the blob has 200 bytes of its 13th sector spare: nothing is bought |
| `vesa_row`'s +16 | - | it is PCIETHER's `drv_tab` row (DRVR_SIZE 16, a layout CTRL.DRV and HIBER.DRV's `DRV_ATTACH` read), not code |
| `drv_e9` 'Class already in use' | - | CTRL.DRV's (`.modu`), a message: shortening it is a function change |
| `mega_npx`'s two seven-pop exits as one (`db 0xA8 / stc`, pops, `jc` the fault) | -3 `.cold` (kern_mega) | free and priced, NOT TAKEN: the #NP dispatch is reached only by `win16np`/`win16ovr`, which need Bochs and the Open Watcom fixtures, neither in this container; the next pass with them can take it |
| `mega_i21`'s `.none` storing `KERNEL_SEG:kbm_isr` into the slots and sharing the `retf` exit | -1 `.mega` | as above; and the `.mega` byte would fall into `align 16` |
| `mega_segty`'s second `mov di, dx / and di, 0xFFF8 / mov es, LDTD` (the handle path re-derives what it already holds before `.set`) | ~-11 `.cold` (kern_mega) | the `.f` (window) arm needs the same setup, so sharing it means re-ordering the walk; untestable here (Bochs) for a kern_mega-only `.cold` with 25 bytes of rung left either way |
| PCIETHER: `rt_tx` / `rt_rx` banking less | ~-10 | nic_tx and nic_rx are the stack's verbs, and `eth_pump_i`'s STKDEPTH-NOSAVE marker (and inet.inc's callers) rely on them giving AX, BX, DX, SI and DI back |
| ethdrv.inc: `eth_pkg`'s `.notk` and `.busy` as one `stc / jmp .pop` (`cmp ax, imm16` swallowing the second `mov`) | -2 | an obscure shape for two bytes on a cold path; not worth the reading |

## WANTS THE OWNER

1. **`eth_v_rawtx` counts every raw frame twice in `[eth_ntx]`** (DEFECTS
   FOUND 2): nic_tx already counts the frame (`ne_tx`, `rt_tx`), and
   `ethsock.inc`'s raw send adds one more after it. Taking the second
   `inc word [eth_ntx]` out is -4 on both drivers and makes the Control
   Panel's transmit count true - but it changes a number a user reads, so it
   is the owner's.

## WANTS AN ABI BREAK

None found in this row. (`DQ_MAXREC` 11 -> 12 came in with `eb1caa77` and is
an SDK constant that grew, not a break.)

## CROSS-FILE

- `ld_pkg_byname` (loader.inc, `dir`'s row): `mov word [assoc_pwin],
  ASSOC_PWBANK` is six bytes with AX = 0 in hand (`xor ax, ax` just above,
  and AX must be 0 again for the `cmp [ld_base], ax` after): `dec ax / mov
  [assoc_pwin], ax / inc ax` is five. -1, odd in `.cold` ahead of `dsk_xfer`,
  so it wants a partner.
- `fresh-routines.md` lists `kbm_isr` as `main`'s although `mouse.inc` is
  `draw`'s file; nothing else of mine touches mouse.inc.

## ROWS RUN

Once, after the last code commit (`46c8c00a`), one `python3 tools/os88test.py
soak -k ... --marty-jobs 1`: **12 passed, 0 failed, 1 skipped** in 238 s.

| row | what of this note it covers | result |
|---|---|---|
| `kbdpreempt` (MartyPC) | `kbm_isr`'s chain: the ROM called with IRQ1 alone open, 259 tick-in-handler trials, Pause | ok 46.2 s |
| `kbdburst` (MartyPC) | a scripted burst through `kbm_isr` without nesting | ok 33.8 s |
| `kbd4f` (QEMU, an 8042) | the int 15h AH=4Fh arm (`kbm_i15` into `.scan`, the self-chain test, `.jmp9`) | ok 3.8 s |
| `win16nedos` (MartyPC) | leg D: the posted `\AUTOSTRT` launch that keeps the document's locator (ASSOC_PWBANK, item 3) | ok 62.4 s |
| `assocstale`, `assocfind`, `assocopen`, `assocwake` (MartyPC) | `assoc_run_x`: `.nofile`'s rebuild and resweep (`assoc_ownex`), the search, the handover | ok 11.3 / 18.9 / 11.0 / 14.9 s |
| `pciether` (QEMU, kern_mega, RTL8139) | PCIETHER.DRV whole: `drv_f_pcieth` found by name, `pci_probe`, `rt_claim`/`rt_init`/`rt_start`, DHCP, a page fetched through `rt_rx`/`rt_tx`/`rt_es`/`rt_consume`, no overflow reset | ok 21.9 s |
| `ethernet` (QEMU, kern_big, NE2000) | ETHER.DRV through the shared `ethdrv.inc` (item 6) | ok 13.1 s |
| `stknosave`, `stknosave-pciether` | every STKDEPTH-NOSAVE marker in both drivers still holds | ok 1.2 / 1.0 s |
| `win16nowin` | `assoc_ownex`'s refusal (54.4.2.4) | **SKIP**: needs the Open Watcom fixtures, not in this container |

**Broken on purpose** (`pciether`, new to this agent): `rt_port`'s `cbw`
replaced by `mov ah, 0x5A` - the row went red in 43.7 s ("eth_nrx 0 /
eth_ntx 4: the card moved no frames", 1,163 overflow resets, the server
never reached); the source was restored and `build/pciether.bin` compared
byte-identical to the committed assembly.

Gates at each commit: `make -j2` (the fast tier, 73 passed / 1 skipped,
`hotpar` green on kern_big and kern_small, `megaseg` at 3,684), `make -j2
small`; `make emu` and `make mega` at the tip; `tools/stkbalance.py` over
every touched file, base against tip, the same paths (mouse.inc 4,
assoc.inc 3, driver.inc 2, mega.inc 0, ethdrv.inc 1 - `eth_dropall`'s
shared `ep_dx` tail - and 0 in each pciether file); `checkdocs` after `git
add`; `t_guidelock` 0 owed.

**Not run, and why**: the `.mega` items (4) and kern_mega's `.cold` are
reached by `win16ovr`, `win16np`, `win16csalias` and the other `mega-*`
rows, which need Bochs (not installed here) and the Open Watcom fixtures
(not built); they are proved by reading in item 4, and the coordinator's
union with Bochs present should include `win16ovr` and `win16np`.

## DEFECTS FOUND

1. **`docs/INDEX.md` was stale at the brief's commit** (pass 15, 16 and 17's
   lesson again): `make` failed in this fresh worktree at `checkdocs`.
   Regenerated in this branch's first commit; every agent's branch will
   carry the same regeneration and the merges will conflict on it.
2. **Raw frames are double-counted in `[eth_ntx]`** on both drivers (above,
   WANTS THE OWNER 1). Found reading `rt_tx`'s counter against its callers;
   not fixed.
3. Not a defect, a hazard: kern_mega's conventional image rung has one byte
   left (BASE AND TIP).
