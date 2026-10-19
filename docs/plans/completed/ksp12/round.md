# ksp12 agent "round" - the rest of the round's unpassed kernel work

Kernel size pass 12. Concept: five commits on `elendilon` and not on `main`
(so every interface they made may change), none of which had a size pass:

| commit | what | resident it added (its own message) |
|---|---|---|
| `477376be` | Saver vs FTPD: `fpg_arm` refuses under `[blk_on]`, `OSAPI_WM_QUIET` (0x0473), `ss_quiet` | big `.text` +35 `.cold` +28, small `.text` +35 |
| `145c25cc` | ETHER.DRV's TCP send window; FTPD polls its ACK | kernel 0 (`driver.inc`'s `DRVM_ETH` is a constant); ETHER.DRV image |
| `a0fa096c` | Video Player plays on the display that CAN: `fsx_caps` BX = 1..15, `fsx_run` CH | big `.text` +36, EXTD.DRV +8; small +11 |
| `f2e525d9` | `vid_apply`: no first-tick stores past the plain head | +14 both |
| `5b931426` | Uncompress To...: Enter re-reads a swapped-back floppy | DISKOPS.DRV +63 |

So the resident population is **~113 bytes on kern_big and ~60 on
kern_small**; 30% of it is 34 and 18. Branch `ksp12-round`, cut from
`kernel-size-12` at `df8603a1`. Figures are assembled bytes,
`tools/kernsize.py --json`, against the base's absolutes.

## BASE AND TIP

| | base `df8603a1` | tip | delta |
|---|---:|---:|---:|
| kern_big `.text` / `.cold` | 43,738 / 38,690 | 43,706 / 38,684 | **-32 / -6** |
| kern_big `.bss` / `.lowbss` / `.vgabuf` | 5,212 / 5,598 / 336 | same | 0 |
| kern_big resident | 93,574 | 93,536 | **-38** |
| kern_small `.text` | 31,967 | 31,921 | **-46** |
| kern_small resident | 60,413 | 60,367 | **-46** |
| kern_emu `.text` / `.cold` | 43,997 / 38,690 | 43,965 / 38,684 | **-32 / -6** |
| kern_emu resident | 93,833 | 93,795 | **-38** |
| `KERN_SIZE` big / small / emu | 99,328 / 62,464 / 99,840 | same | no rung moved |
| `.boot2` / `.ovl` | | | 0 on all three |
| ETHER.DRV image (the header's `image`, = the claim) | 17,122 | 17,078 | **-44** |
| ETHER.DRV file (lz4) | 11,305 | 11,296 | -9 |
| DISKOPS.DRV image (kern_big, unpacked) | 10,352 | 10,347 | **-5** |
| DISKOPS.DRV image (kern_small) | 7,184 | 7,184 | 0 (the join is kern_big's) |
| EXTD.DRV | 1,031 | 1,031 | 0 |

The ROM arm's window (SPEC.md 2.10): `.cold` 38,690 -> 38,684, so the tail
and spare `kernsize` prints go 2,254 -> 2,260.

Against the concept: **kern_big 38 of ~113 (34%), kern_small 46 of ~60
(77%)**. Every kernel batch was `make -j2`, `make -j2 small` and the fast
tier (43/43 each time), and `make emu` at both commits. `tools/stkbalance.py`
over every file touched, base against tip: the same 27 (kernel files) and 8
(ETHER + compress.inc) reports by routine, nothing new.

## TAKEN

### Batch 1 - the kernel (`e1edbeb5`)

* **`vid_apply`'s first-tick skip is the SS test it already had** (-14, both
  kernels). `f2e525d9` added `cmp byte [spl_live], 0 / je .home / cmp word
  [spl_fseg], 0 / je .homed` to keep the splash's first tick from storing
  past the plain head. Ten lines above it the routine already asks the same
  question a different way: `mov ax, ss / cmp ax, LOW_SEG / jne .norowtab`
  keeps the row table off the boot sector's stack. The only caller that runs
  off `LOW_SEG` is `spl_chrome` on stage 2's first tick (`spl_tick` is stage
  2's alone - `boothd.asm` and `osrom.asm` never call it, and every later
  caller, `vid_init`, `vid_switch` and the arrangement code at vidsel.inc
  2060, is downstream of `kmain`'s `mov ss`). So `jne .norowtab` became
  `jne .homed` and the 14-byte test went. **Equivalence, by reading every
  caller**: on the first tick both forms skip the row table AND the stores;
  on every other call SS = `LOW_SEG`, and the old test could only skip the
  stores where `[spl_live]` = 1 with `[spl_fseg]` = 0, which no
  `LOW_SEG` caller sees (vid_init runs with the blob published; after
  `mem_unblob` `[spl_live]` is 0). The new form never skips more than the
  old one on a whole kernel. `fcpsmall` - the row that caught the original
  defect - `small128` and `kzboot` are green. SPEC.md 2.9's paragraph is
  updated to say what the test now is.
* **The homing walks `vid_w`/`vid_h` with `lodsw`** (-1, both): the two are
  adjacent (an `%if` now pins it) and SI is dead on the one arm that reaches
  it.
* **`[blk_on]` is the byte after `[spl_live]`** (-7, both): `477376be`'s
  `fpg_arm` test was a second `cmp byte / jne .no` beside the splash's own;
  moving the byte's definition from blank.inc into viddet.inc (the plain head,
  which only grows by the byte blank.inc loses) makes the two refusals one
  `cmp word [spl_live], 0`. An `%error` pins the pair. Every other reader
  names `blk_on` by symbol.
* **`fsx_caps`** (-6 big, -3 small): `cmp bx, 16 / jae .ask / cmp
  [vid_ndisp], bl / jb .out` in place of the `lea`/`cmp ax`/`cmp al` chain -
  BX = 0 compares below nothing and asks - and the refusal's own CF goes out
  through the shared `pop bx` (`clc` moved above it), so the private `pop
  bx / stc / ret` went. kern_small's `lea ax, [bx-2] / cmp ax, 14` takes the
  same exit with `jb`.
* **`fsx_run`** (-2, big): the door no longer clears CH (`xor ch, ch`); only
  CL is read until the release loop, which now loads all of CX (`mov cx,
  INST_MAX`, kern_big only - +1 byte for -2). The display resolve is `dec
  byte [fsx_wdisp] / jns .disp` with the store inside the call arm (-1).
* **`wm_quiet`** (-2, both): clear the bit and then set it if asked - one
  fall-through, no `jmp`. The bit is read only by `ss_start_x`, on the UI
  task, and FTPD's set and clear are clicks on it, so nothing can see it
  clear in between. Flags are still preserved (see REFUSED).
* **`ss_quiet` written at its one call** (-6 `.cold`, big): and walked by the
  pointer against its end rather than by a CX count, so the `jb` that keeps
  going is also the CF = 0 that falls out (`clc` gone, `push cx`/`pop cx`
  gone, `call`/`ret` gone).

### Batch 2 - kern_small's `OSAPI_WM_QUIET`, ETHER.DRV and DISKOPS.DRV (`9d0b3591`)

* **kern_small's `wm_quiet` is a bare `ret`** (-19, kern_small): the bit it
  set is read in one place, `ss_start_x`, which is kern_big's (kern_small
  has no animation). The cell stays in both kernels (SPEC.md 20.8 rule 4)
  and the SDK's own words for kern_small - "takes the call and ignores it" -
  are now literally the body. Every register and the flags kept, as before;
  no caller anywhere can tell, since nothing on kern_small reads W_FLAGS bit
  12 (and FTPD, the only caller, is not on the small disks - SPEC.md 24.5).
  SPEC.md 79.10's kern_small sentence updated.


Second priority, so after the resident bytes. **ETHER.DRV -44** (the
`image` field, which is the claim drv_load cuts: 17,122 -> 17,078); the
compressed file 11,305 -> 11,296. The stored (non-bss) part reads 16,386 ->
16,390 because the zero run before the bss shrank by more than the code did -
the claim is the RAM figure. Per routine, off the two listings: `tcp_retx`
-81 / `tcp_timers` +69, `tcp_ackin` -20, `sk_txpos` -8 / `sk_txfold` +12,
`tcp_kick` -6, `tcp_seg` -6, `tcp_abort` -4, `tcp_out` -3, `eth_v_send` -2,
`tcp_wnd` -2, `tcp_arm_idle` +7 = **-44**; tcp_ackin's figure includes 6 bytes
of its own jumps coming into short reach. **DISKOPS.DRV -5** on kern_big
(the join, and so `cmz_jfresh`, is kern_big's alone).

* **`tcp_wnd` is called by `tcp_ackin`** (-5): both of `tcp_seg`'s calls to
  it sat immediately before `call tcp_ackin`, and those are tcp_ackin's only
  two callers, so it is one `call` at tcp_ackin's head - before the TF_ACK
  test, as before - and the routine loses its AX bank (tcp_ackin banks AX,
  and `tcp_accept_in`, the third caller, overwrites AX on the next line).
* **`sk_txfold`** (-7): `145c25cc` wrote the ring fold twice - `sk_txpos`'s
  first four instructions, and tcp_ackin's new front. One routine now; the
  front is `jz .empty / call sk_txfold / jmp short .front / .empty: xor ax,
  ax / .front: mov [bx+SKO_TXT], ax` (12 bytes where it was 23). The `.empty`
  arm leaves AX = 0 where it left the retired count; nothing after
  `.nodata` reads AX (`tcp_finacked` banks its own).
* **`tcp_retx` written at its one call** in `tcp_timers` (-12): AX, CX, SI
  and DI are dead in that loop, so DX - DL is the slot index - is the only
  bank the body owes, and its `ret` is `jmp short .next`. For CX to be dead
  the loop runs on its index (`inc dl / cmp dl, NET_SOCKS / jb .l`, the same
  7 bytes as `mov cx, NET_SOCKS` + `loop`). The body sits after the loop's
  `ret`, so `loop`'s reach is not spent on it. **Equivalence by listing**:
  the base's `tcp_retx` and the tip's `.retx`, instruction text diffed,
  differ in the four push/pop pairs and the exit and in nothing else.
* **`tcp_arm_idle`** (-5): `tcp_kick`'s and `tcp_ackin`'s "arm unless
  already timing the oldest byte" is one 7-byte routine in front of
  `tcp_arm`. In tcp_ackin the RTO test moves from before the TXS/flag tests
  to after them; the three are a conjunction with no side effect between.
* **`tcp_abort`'s four zero stores through AX** (-4): 16 bytes where four
  immediates were 20, AL (the state) banked round them.
* **`tcp_out`'s wrap arm takes `eth_v_send`'s shape** (-3): `xchg cx, ax`
  and `sub ax, cx / mov cx, ax` instead of `mov cx, ax` and a reload of
  `[eth_dlen]` - ring_out keeps both.
* **`eth_v_send`'s `push cx`/`pop cx` round `tcp_kick`** (-2): tcp_kick
  banks CX itself.
* **DISKOPS.DRV `cmz_jfresh` calls `dsk_rah_flush`** (-5): `5b931426`
  copied the flush's loop into the image on the belief that the routine was
  `.text` and so out of a module's reach. It is `.cold` (disk.inc's section
  at line 6086, on BOTH kernels - checked in both listings), so it is one
  `CLO_KCALL` away, the diskops.inc macro HIBER.DRV's precedent uses (`mov
  bp, dsk_rah_flush / call hbm_cb`). BP is banked round it, the contract
  being "clobbers BX CX". dsk_rah_flush clears every record's RAH_N exactly
  as the copy did and keeps every register and the flags.

## REFUSED

* **`OSAPI_WM_QUIET` without its `pushf`/`popf`** - 2 bytes, both kernels.
  The cell's contract says FLAGS are kept, and the only caller (FTPD's
  `fd_quiet`, from two clicks) never reads them, so the reshape is legal
  for an ABI not yet on `main`. Refused for `wm_snap`'s reason, written at
  its own `pushf`: a package's ENTRY PROC returns CF to the loader, and a
  server that asks for the blanker at launch is exactly the caller this cell
  is for. Two bytes against a launch that fails as `Load failed`.
* **Dropping the cell and having FTPD write `WF_QUIET` itself** - ~26 bytes
  both kernels (the 6-byte rare cell and `wm_quiet`'s 20). Refused: wm.inc's
  own header on `wm_noanim` says a settable W_FLAGS bit is a slot's to write,
  and `apps/paint`'s `pt_wfix` is the one hand-write left in the tree with a
  paragraph on why it cannot be given up; this would make a second for 26
  bytes.
* **`ss_start_x`'s walk counted in AL instead of compared against its end**
  - 0 bytes (`mov al, MAX_WIN` + `dec al / jnz` is the same 6 as `cmp bx,
  imm16 / jb`). Left as the pointer compare, which needs no register.
* **The join's CLV_FREE wrapper** (`5b931426`, DISKOPS.DRV, 8 pushes and 8
  pops round `call .run`): a narrower bank would need the clobber set of
  `cmz_jrmtmp`, `cmz_jexit`/`cmz_jundo` and `cmz_jerr`'s unwind, which reach
  the file layer. The commit's own defect was a too-narrow assumption about
  exactly that; not taken on a reading.
* **EXTD.DRV's `ext_e_fsx` display arm** (`mov al, bl / dec ax / cmp bx, 16 /
  jb .kind`, 8 bytes): already minimal - `lea ax, [bx-1]` is the same 3.

## WANTS AN ABI BREAK

None found in this concept: everything it published (`OSAPI_WM_QUIET`,
`OSAPI_FSX_CAPS`'s BX = 1..15, `OSAPI_FSX_RUN`'s CH, `DRVM_CEIL_NET`) is
either still `elendilon`'s or an extension a frozen caller cannot see, and
nothing here got cheaper by re-contracting it beyond what is taken above.

## CROSS-FILE

For the cross-file agent - found while working, not in this concept's
lines, not taken:

* **`wm_noanim` on kern_small is the same case as `wm_quiet`** (wm.inc,
  `main`'s cell 0x03CD - FROZEN, but this changes no contract): WF_NOANIM is
  read only by the zoom (`wm.inc` ~11192, kern_big), and since `fdlg.inc` and
  `ui.inc` stopped writing it by hand `wm_noanim` is its only setter. On
  kern_small the body (`call wm_isrec / jae / or word [bx+W_FLAGS],
  WF_NOANIM / ret`, 10 bytes) could be a bare `ret`: **-9 kern_small**,
  estimated from the encoding. Its own header argues for keeping the body in
  both kernels "for [wm_animon]'s reason" - that reason is about the CELL
  existing, which it would.

## ROWS RUN

All MartyPC rows with `--marty-jobs 1`, through `tools/os88soak.py start
-k ...` on its own frozen tree; ETHER.DRV's on QEMU, MartyPC having no NIC.

* **At `e1edbeb5` (batch 1), soak, 12/12 ok**: `fcpsmall` (the row that
  caught `f2e525d9`'s defect - the first tick's stores, on the 1.44MB
  kern_small), `small128`, `kzboot`, `saver`, `dispsaver`, `vidxdisp`,
  `dispfsxcga`, `dispfsxherc`, `vidcga4`, `extdmod`, `fsxdisp`, `curdisk`.
* **`vidxdisp` RED on purpose**: with `fsx_caps`' display arm made to refuse
  every BX = 1..15 the row fails exactly where it should - `A: a CGA4 file on
  the Hercules is ok 0, display 0 - not the CGA's: 'CGA colour: needs a CGA
  or VGA'` - and is green again restored.
* **`tests/ftpd.py`** (QEMU, `make ftpdtest`; unregistered - t_registry
  names why): the twelve assertions, the ACTIVE-mode and trusting-client
  legs included. **Interleaved A/B, base then tip three times: 3/3 and
  3/3 ok.** Earlier, run in blocks while the box was loaded, assertion 8b
  (an ACTIVE-mode STOR/LIST whose reply never arrives, an ftplib socket
  timeout) failed **base 1 in 5 and tip 4 in 8** - the same leg, the same
  timeout, on both; see DEFECTS.
* **`tests/ethernet.py`** (QEMU, `make ethertest browsertest`): ok - DHCP,
  the browser's fetch, `frames rx 7 tx 9, segments 6, dropped 0`.
* **At `9d0b3591` (batch 2), soak, 2/2 ok**: `czto` - its `same` leg is the
  one `5b931426` made red without `cmz_jfresh`, so it drives the
  `dsk_rah_flush` call - and `small128`.
* The retransmit path (the inlined `tcp_retx`) is reached by no harness here:
  slirp does not lose segments, and `tools/ftptap.py --loss`, which does,
  needs iproute2, dnsmasq and iptables, none of which this box has.
  Equivalence there is the listing diff above.
* Fast tier after every batch: 43/43.

## DEFECTS FOUND

* **No behaviour defect in the concept.** One wrong COMMENT cost bytes:
  `5b931426`'s `cmz_jfresh` said `dsk_rah_flush` "is .text, and this is an
  image", and copied its loop on that belief; the routine is `.cold` on both
  kernels and was always one `CLO_KCALL` away (taken, -5).
* **`tests/ftpd.py` assertion 8b is intermittent ON THE BASE** (not fixed,
  not this concept's): under load an ACTIVE-mode STOR or the LIST after the
  four collapsed-name STORs waits out ftplib's 30 s for a reply - 1 in 5 at
  `df8603a1` and 4 in 8 at the tip when run in blocks, 0 in 3 each when the
  two were interleaved. It is not registered as a row, so nothing in the
  suite would show it; whoever next touches FTPD's active-mode data path or
  the harness's timeouts should take a rate rather than a single run
  (`tools/os88bisect.py`'s point).
