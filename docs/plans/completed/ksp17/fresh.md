# ksp17 agent "fresh" - the work since pass 16, and FSCHECK.DRV

Kernel size pass 17 (`docs/plans/completed/ksp17/BRIEF.md`, row `fresh`):
every hunk `git diff 920b839b af5579d2 -- kernel/ boot/` shows and the
routines they sit in - `dsk_xfer`'s 64KB-page cut and `disk_write_x`
(`e10d32c5`), the near-call filter's resident half (`lz_decomp_big`,
`kernel/lzfilt.inc`), System Check's kernel half (`osapi_vol_check`,
`fsk_cnear`, the `.bss`, `apic_fs_op`'s cell, the mod map row) and the image
it ships, FSCHECK.DRV (`kernel/fsck.inc`'s `.modfs`), and the
`assoc_run_x` / `ld_pkg_byname` / `mod.inc` / `kernel.asm` hunks of the DOS
hand-on. Branch `ksp17-fresh`, base `00348bdc` (the brief, on `af5579d2`).
Figures are ASSEMBLED bytes, `tools/kernsize.py --json` absolutes; resident =
`.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf` + `.vga` (+ `.mega` on
kern_mega). Module images are the UNPACKED image (`os88drv.image_unwrap`).

**TAKEN outside the row**: none. `dsk_xfer`'s `.drv` arm (item 2) is in the
routine the `e10d32c5` hunk sits in, and is the driver arm that hunk feeds;
nothing in the BIOS per-sector loop was touched.

**The finding**: the +180 of `dsk_xfer` was mostly a SECOND COPIER.
`.drvcut` stages the one straddling sector through `dsk_secbuf` exactly as
`.bounce` does for a BIOS volume, and `.bounce` already has a copier that
takes the caller's sector as a far pointer in `[dsk_bnc_bx]` - so the 36-byte
`.dhcp`, which the hunk had widened from kern_mega to kern_big, goes back to
kern_mega and `.drvcut` stages through `.bcp`. On System Check's side the
image was carrying a value nobody changes: ES is the workspace from
`fsk_entry`'s `mov es, dx` to its return, so the kernel's `[fsk_ws]` and the
image's fourteen reloads of it are the same bytes as nothing.

## BASE AND TIP

| | base `00348bdc` | tip | delta |
|---|---:|---:|---:|
| **kern_big resident** | 91,879 | **91,792** | **-87** |
| kern_big `.text` / `.bss` / `.cold` | 40,925 / 5,435 / 37,793 | 40,925 / 5,429 / 37,712 | 0 / -6 / -81 |
| kern_big `.boot2` / `.ovl` / `KERN_SIZE` | 6,455 / 5,116 / 97,280 | unchanged | 0 |
| **kern_small resident** | 57,718 | **57,718** | 0 (no hunk of the row assembles there) |
| **kern_emu resident** | 92,133 | **92,046** | **-87** (`.bss` -6, `.cold` -81) |
| **kern_mega resident** (incl. `.mega` 21,712) | 125,183 | **125,146** | **-37** (`.bss` -6, `.cold` -31) |
| **FSCHECK.DRV image** (`.modfs`, kern_big) | 5,679 | **5,424** | **-255** (not resident) |
| FSCHECK.DRV file (lz-packed, `build/fscheck.drv`) | 4,657 | 4,566 | -91 (disk bytes) |

kern_mega's `megaseg` overhead is 3,248 of the 3,250 `tests/megaseg.txt`
allows (no `.text` moved there). No 512-byte rung moved on any kernel.

By routine (kern_big, `tools/os88rdiff.py kernel 00348bdc .`):
`disk_write_x` 22 -> 20, `dsk_xfer` 938 -> 888, `osapi_vol_check` 73 -> 44,
`.bss` -6 (`fsk_fp`, `fsk_ws`).

## TAKEN

### 1. `.drvcut` stages the straddler through `.bcp`; `.dcn` divides as AH/2 (kern_big -39, emu -39, mega 0)

`.dcone` puts the caller's DX:BX in `[dsk_bnc_bx]`/`[dsk_bnc_es]` and calls
`.bcp` (AL = 03h in, 02h out) where it called `.dhcp` behind two compares of
its own; `.bcp` tests `[dsk_op]` itself and spends ES and CX, which `.dcone`
banks. `[dsk_bnc_si]` stays 0, so `.unbounce` and `.fail` see no bounce; the
pair is module scratch under `sch_lock`, and no driver call between the two
copies re-enters `dsk_xfer` (a mounted image takes `.img`, never `.drv`).
`.dhcp` is `%ifdef KERN_MEGA` again (its other caller is `.drvhi`). CF after
the out-copy is 0 either way: `.bcp`'s compare is 2-2 or 3-2.

`.dcn` took `left / 512` as `mov cl, 9 / shr ax, cl` inside a CX bank; it is
`mov al, ah / shr al, 1 / cbw` now (`left` < 64K, so the quotient < 128 and
`cbw` zeroes AH), and the bank closes after the `shl ax, cl` by 4.

**Cycles** (8088, the driver arm only - the BIOS per-sector loop is
byte-identical): an aligned run's `.dcn` is the same instructions reordered;
an unaligned run's `.dcn` is ~40 cycles cheaper (`shr ax, cl` by 9 is
8 + 4x9). The straddler's own path, the rare arm, pays `.bcp`'s compare in
place of `.dcone`'s, so it is a wash.

### 2. `.drv` builds DSV_BLK's AX only for kern_mega (kern_big -11, emu -11, mega 0)

`.drv` loaded `AH = [dsk_unit]`, `AL = [dsk_op] - 2` and banked it across
`drv_cls_svc_x` for the call that followed - but on kern_big that call is
`.drvcut`, which reads no AX on entry (`.dcn` overwrites it) and has
`.dcall` build the pair per driver call. The build, the push and the pop are
under `%ifdef KERN_MEGA` now, where `.drvhi`/`drv_blk_call_x` take AX as
built. **~55 cycles faster per driver-volume run** on kern_big
(`mov ah, [m]` 14, `mov al, [m]` 10, `sub` 4, `push` 15, `pop` 12).

### 3. `disk_write_x` banks no flags around the generation bump (all three -2)

`inc byte [bx+dsk_wgen]` leaves CF, and the only other flags it moves are
ones `dsk_xfer` never reads before setting them and always sets again on the
way out (`dec byte [sch_lock]` at `.out` is every exit). So `pushf`/`popf`
went: -2 bytes, 18 cycles off every write call.

**Parity**: 1 to 3 together shift `dsk_runcap` and `dsk_next_clus_x` by
-52, EVEN (item 1 alone was -41 and failed `hotpar`; item 2 was the
legitimate byte that evened it, not a pad). No new pad; the `.img` arm's
existing `nop` is untouched.

### 4. `osapi_vol_check` (`.cold` -29 and `.bss` -4 - `[fsk_fp]` - on kern_big, emu and mega)

- **BP is api_rsc's to restore** (`OSAPI_RCSLOT`: "BP is an INPUT to no
  cell of this shape"), so the `push bp`/`pop bp` around each `spw_near` went
  (-2, and -2 more on kern_mega).
- **The header is mod_check's own compare**: `mod_hdrt` is the image header's
  first four words with its id byte stamped per call, so the stub stamps
  `MOD_FSCK` there and asks `repe cmpsw` of four words, where it asked a
  `cmpsw`, a `cmp word [es:2], imm16` and a two-word `repe cmpsw` (-13).
  Byte-identical verdicts: the four words are the same four, in the same
  order. The stamp is a UI-task store like mod_check's own, with no yield
  between it and the compare.
- **The image is entered by a FAR JUMP**: `push <segment> / push word
  [es:MOD_H_ENT] / retf`, so the image's own `retf` returns to `api_rsc`,
  which the stub's `call far [fsk_fp] / retf` reached next anyway. `[fsk_fp]`
  (4 bytes of `.bss`) and the three stores into it go (-11 `.cold`). The
  segment is ONE `push FSK_CSEG` line (`es` on kern_big, `bp` - the code
  selector - on kern_mega), `api_far`'s trick, because `tools/stkbalance.py`
  reads the source and not the `%ifdef`; the first shape, with a kern_mega
  arm patching the frame, read as depth +3 there. One stack frame shallower
  through the whole check.

### 5. FSCHECK.DRV's ES invariant, and `[fsk_ws]` (resident `.bss` -2 on all three; image -64)

`fsk_entry` does `mov es, dx` for every verb but FSK_SIZE (which touches no
workspace), and nothing in the image loads ES again: `KC` banks it round
every kernel routine, `fsk_qpop` and `fsk_ph_fatcmp` copy it into DS and
leave it. So the fourteen `mov es, [fsk_ws]` reloads loaded the value ES
already held, as did `fsk_q`'s and `fsk_set`'s banks; all went, with the
kernel's `[fsk_ws]` and `fsk_entry`'s store into it. The file's comment says
why. (`inst_caller`, the one kernel routine the image reaches outside `KC`,
writes only DH.)

### 6. FSCHECK.DRV finder (image -191 more; nothing resident)

- `fsk_q0` / `fsk_q0ds`: entries in front of `fsk_q` that zero DX, DI, SI /
  DX, SI - sixteen callers wrote the zeros out; every one of them zeroed after
  any `fsk_qp_*` that might read them, which none does.
- `fsk_fix` (BH = the Fix bit, ZF with it) for six spellings.
- `fsk_rootsec` / `fsk_rdlba` / `fsk_rd1d` / `fsk_rd0c` / `fsk_rd1c` falling
  into `fsk_rd`: eight one-sector reads loaded BX, CX and the LBA by hand.
- `fsk_isend` is `fsk_isbad`'s compare complemented, less the bad mark:
  `AX >= end` is `AX >= bad and AX != bad` on both FAT widths.
- `fsk_fatdx` (DX = the entry, AX kept, ZF = free) for five `push ax / call
  fsk_fat / mov dx, ax / pop ax` shapes; `fsk_defmap`'s DX is dead there.
- `fsk_save` zeroes the entry's bytes 12-21 with `rep stosw` (DF is clear
  through the call: `fsk_qpop` and `fsk_ph_fatcmp` already rely on it).
- `fsk_defstart` banks no BP around `spw_near` (BP is only `KC`'s scratch).

## REFUSED

- **`lz_decomp_big`'s filter hunks** (+44) are as tight as found: the escape
  test as `inc ax / jnz` is the same 8 bytes; the trailing `clc` is needed
  because all three of `LZ_UNE8`'s exits can leave CF set; `sub cx, 2` cannot
  be `dec cx / dec cx` (no CF, and a signed test is wrong past 32K of output);
  `loop` needs a `jcxz` guard that costs what it saves.
- **`.drvcut`'s loop exit at `sub cx, ax / jz`** (drops `.dcok`'s `clc`):
  +1 net, and correct only by leaning on `drv_cls_svc_x`'s CF four
  instructions up (CLSCF). It would have evened item 1's parity at a byte's
  cost; item 2 did it for -11 instead.
- **Advancing BX instead of the segment in `.dcadv`** (`xchg al, ah / shl
  ax, 1 / add bx, ax`, -2): SPEC.md 18.91.4.1 says the SEGMENT advances, and a
  caller's buffer may run past its own offset's end (a >64KB load module
  read in one call).
- **`fsk_cnear`** (3 bytes of `.cold`): the image needs a `call bp / retf` in
  COLD_SEG and this is the only one there (`spw_near` and `mega_tcall` are
  `.text`, `mega_bpcall` `.mega`).
- **`[fsk_sp]` and `[fsk_cold]` into the workspace** (-6 `.bss`): FSK_SIZE
  has no workspace and can still die through `fsk_die`, which reads both.
- **`assoc_run_x`'s `.fwd`** (12): the hand-on must skip both the toast and
  `.refresh`, and the only shared tail it could take (`assoc_back` then
  `cmp [ld_painted]`) repaints. **`ld_pkg_byname`'s hunk** (10): nothing
  shorter.

## WANTS THE OWNER

- **One write generation for the machine instead of one per volume**
  (resident -13 on kern_big/emu/mega: `.bss` `dsk_wgen` 8 -> 1, and
  `disk_write_x`'s bump is `inc byte [dsk_wgen]`, 4 bytes for 10; FSCHECK.DRV
  `fsk_gen` -9). The function change: a write to ANY volume - B: while A: is
  checked - would end a check with FSKE_CHANGED where today only a write to
  the volume under check does. SPEC.md 118.14.6 says per volume.

## WANTS AN ABI BREAK

None. `OSAPI_VOL_CHECK` (0x04C8) is not on `main` and was left as its
contract reads; the stub's frame is one far call shallower, which no caller
can see.

## CROSS-FILE

- **The generation bump inside `dsk_rah_wr`** (disk's, -4 on kern_big/emu/
  mega): it already banks BX and the flags and loads `[disk_drive]`, so
  `mov bl, [disk_drive] / mov bh, 0 / inc byte [bx+dsk_wgen] / mov dh, bl`
  there replaces `disk_write_x`'s 10 bytes and `dsk_rah_wr`'s own 4-byte
  load. NOT byte-identical: `.img` calls `dsk_rah_wr` for an image write's
  HOST sectors, so the host volume's generation would move too (today only
  the image volume's does). Arguably the right answer for a check of the host
  - but a function change, so it is the owner's with the item above.
- **`dsk_unit` the byte after `dsk_op`** (disk's `.bss`): `mov ax, [dsk_op]`
  would replace `mov ah, [dsk_unit] / mov al, [dsk_op]` in `.dcall` (-4) and
  in kern_mega's `.drv` arm.

## ROWS RUN

Once, after the last code commit (`python3 tools/os88test.py soak -k drvcut
-k fsckdisk -k fsckhd -k fsckmega -k syscheck -k syscheckmono -k fpgop -k
vidhdmake --marty-jobs 1`): see the results line below.

**7 ok, 1 SKIP, 0 FAIL** in 374 s: `drvcut` ok 38.6s, `fsckdisk` ok 82.0s,
`fsckhd` ok 31.1s, `syscheck` ok 6.6s, `syscheckmono` ok 45.8s, `fpgop` ok
90.3s, `vidhdmake` ok 79.7s; **`fsckmega` SKIP (needs bochs)** - kern_mega's
image and stub changes are therefore checked by build only (`make mega`,
`megaseg`), and the row wants a box with Bochs.

`drvcut` was broken on purpose once (`.dcone`'s out-copy handed `.bcp` an op
that never matches): red, *"the program never printed its answer"*. Reverted
and rebuilt; the tip is the kernel the green run used.

`make`, `make small`, `make emu` and `make mega` build at the tip; the fast
tier (`stkbalance`, `hotpar`, `ovlchk`, `megaseg`) is green in `make`.

## DEFECTS FOUND

None in the shipped code. One in this agent's own method, worth the next
reader's minute: an `emu`/`mega` build left running in the background while
`make` ran in the same tree made `os88kz` report *"the two passes packed
DIFFERENT bytes"* - two makes writing one `build/`. The brief's rule
(never two builds in one tree) is the whole fix; the retry was clean.
