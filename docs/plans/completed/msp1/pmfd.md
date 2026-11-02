# msp1 agent "pmfd" - the API stubs, the huge claims and the native floppy

kern_mega size pass 1 (`docs/plans/completed/msp1/BRIEF.md`). Range:
`kernel/mega.inc` lines 4969-8107 at the base - the kern_mega arms of the hot
cells (`mega_sl_*`, `mega_fj_*`, `MEGA_ICW*`), `mega_bhex*`, `mega_rmaskf`/
`mega_vrm`, `mega_hibnc`, the huge-claim code (`mega_huge*`, `mega_hg*`,
`mega_mxat`, `mega_xmrunf`, `mx_runcntf`) and the native floppy (`mega_fd*`,
`MEGA_FDIO`'s two copies). Branch `msp1-pmfd`, base `4fb1e6d5`. Figures are
ASSEMBLED bytes, `tools/kernsize.py --build build/megak --json` absolutes, and
`tools/os88rdiff.py kernel 4fb1e6d5 . -D KERN_BIG -D KERN_MEGA -D KERN_KNOB
--build build/megak` per routine.

Everything in the range is assembled under `mega.inc`'s `cpu 286` (line 191,
to the file's last line), and every routine touched runs only on kern_mega -
so 286 shifts and `imul r16, r/m, imm` are legal there and were used. Nothing
here is on the 286 RESET path (`mega_r286` is pmcore's); the `.text` copy of
`MEGA_FDIO` IS called from it (`mega_fdrprobe`), so that copy's change is
argued by reading below.

## BASE AND TIP

| | base `4fb1e6d5` | tip | delta |
|---|---:|---:|---:|
| kern_mega `.text` | 47,548 | 47,538 | **-10** |
| kern_mega `.bss` / `.lowbss` / `.vgabuf` | 5,435 / 7,679 / 336 | same | 0 |
| **kern_mega conventional resident** (`.text`+`.bss`+`.lowbss`+`.vgabuf`) | 60,998 | 60,988 | **-10** |
| kern_mega `.cold` (above 1MB after the switch) | 44,957 | 44,803 | **-154** |
| kern_mega `.mega` | 18,237 | 18,169 | **-68** |
| kern_mega `.mega` scratch (`mscr`, includes the 16-byte alignment of `MEGA_SCR_OFF`) | 1,493 | 1,497 | +4 (padding only) |
| kern_mega `.boot2` / `.ovl` | 6,620 / 5,109 | same | 0 |
| kern_mega `KERN_SIZE` at boot | 131,584 | 131,584 | 0 (no rung crossed) |
| kern_mega `.text`+`.bss` segment left | 12,553 | 12,563 | +10 |
| **kern_big** `kernel.bin` | | | **byte-identical** (`cmp` at one commit) |

kern_small and kern_emu do not assemble `mega.inc` and were not rebuilt.
Total: **232 bytes** of kern_mega, 10 of them conventional for ever.

## TAKEN

All in `kernel/mega.inc`; one commit, `9dee6752`.

| item | section | bytes | hot? |
|---|---|---:|---|
| `mega_xmrunf`: its private LDT-run scan and its 32KB-step fill were `mega_hgfree` and `mega_hgfill32` written out again; it calls them. Plus `shr ax, 10`/`shl dx, 6`/`shr cx, 5` for the `mov cl, n` shift pairs, and a dead `mov si, ax`. 295 -> 188 | `.cold` | -107 | no (a claim) |
| `mega_hgfill`: the step is BP = 16/32/64 KB and the next base is `BP << 10` with the 64KB carry made by `cmp si, 1 / adc dl, 0`, where three arms each had their own add/adc/sub/jmp. 84 -> 67 | `.cold` | -17 | no |
| `mega_hugec`: the code half is `mega_hgfill` of ONE entry with byte 5 patched to 9Ah - the same limit (64KB or the claim's end), base and attributes it wrote by hand. 174 -> 148 | `.cold` | -26 | no |
| `mega_hugebf`: its six-pop epilogue is `jmp mega_hugef.o` (the same six pushes in the same order) | `.cold` | -4 | no |
| `MEGA_FDIO` (both copies, `.text`'s `mega_fd*` and `.mega`'s `mm_fd*`): `fdout` and `fdin` share their pops with the failure path (CF = 0 already from the `cmp` that matched, so `clc` goes; `fdin`'s timeout leaves CF = 1 from `cmp al, 0C0h` with AL below it, so `stc` goes too); `fdsense` is `mov ah, al / call fdin / xchg al, ah` (fdin keeps AH) instead of push/pop through DX; `fdrprobe`'s two four-pop exits are one. 148 -> 138 a copy | `.text` -10, `.mega` -10 | per command/result byte: **faster** (each success path loses a `clc`, 2 clocks; `fdsense` loses a push/pop pair) |
| `mega_selbase`: one prologue for both tables (`mov bx, LDTD / test al, 4 / jnz / mov bx, WIN`), `xchg ax, bx`, and the `clc` that `xor dh, dh` already made. 46 -> 36 | `.mega` | -10 | 21 callers; **faster** on both arms (286 clocks: LDT 30 -> 28, GDT 27 -> 26 before the common `mov es`) |
| `mega_fd13`: ES reload after `mega_fdgo` dropped (it is still the BDA - `mega_fdgo` and everything it calls, `MEGA_DIAG`'s strip included, balance their ES); drive/head `test dx, 0FEFEh`; CMOS type `dec al / cmp al, 3`; byte count `mov ch, [mega_fdn] / xor cl, cl`. 434 -> 422 | `.mega` | -12 | per transfer; fewer instructions |
| `mega_fdgo`: motor bits `shl ch, 4`/`shl al, 4` (the `and al, 0Fh` before a shift by 4 is dead), no push/pop of CX round them (CL/CH are reloaded before any use), spin-up `imul ax, ax, 9`/`shr ax, 2`. 576 -> 568 | `.mega` | -8 | per transfer, once; fewer instructions |
| `mega_fdirq`: one exit (`and` leaves CF = 0). 40 -> 38 | `.mega` | -2 | per wait |
| `mega_fdtype` `shr al, 4`; `mx_runcntf` `shr cx, 5` | `.mega` | -5, -7 | no |
| `mega_rmaskf`: the mouse vector's IVT cell is a constant (23h or 24h x 4), compared directly. 164 -> 160 | `.mega` | -4 | per service reflection; fewer instructions |
| `mega_hibnc`: a dead `xor ch, ch`; bytes = `mov ah, [mega_hbn] / xor al, al / shl ax, 1`. 312 -> 309 | `.mega` | -3 | no |

**Equivalence, by reading** (no unicorn in this container):
`mega_hgfree` finds the LOWEST run of CX free entries from `MEGA_LDT_FREE0`,
which is what `mega_xmrunf`'s scan found (it restarted one past the run's
START, hgfree one past the TAKEN entry - any run starting between the two
contains the taken entry, so the first fit is the same); both refuse when the
check reaches `[mega_ldte]`. `mega_hgfill32`'s descriptors are byte for byte
the ones `.fill` wrote (limit `KB*1024-1` below 64KB by `shl bx, 10` where
`.fill` did `mov bh, bl / shl bx, 1` twice). `mega_hgfill`'s new step:
for 16/32KB `SI << 10` is 4000h/8000h and `adc dl, 0` carries; for 64KB SI is
0, the add carries nothing and `cmp si, 1` borrows so DL steps by one - the
three old arms exactly; `sub bx, bp` is their `sub bx, 16/32/64`. The
`MEGA_FDIO` change in the `.text` copy (the one `mega_r286` calls in real
mode after a 286 reset) touches only flag and pop ordering: every exit leaves
the same CF and the same registers (fdsense now leaves DX alone where it left
DX = the first byte; no caller reads it - fdgo reloads DX before use, fdseek
and fdrprobe's loop do not read it).

## REFUSED

* **Shifts by 2 for the 765's head byte** (`shl al, 1` x2 -> `shl al, 2`, in
  `mega_fdgo` and `mega_fdseek`): 1 byte each, but `shl r8, imm` is 5+n
  clocks on a 286 against 2+2 - slower on the native floppy's command path.
* **Moving the `mega_fd*`/`mega_hb*`/`mega_rmask` variables from `.text` to
  `.bss`** (~60 bytes, all but two zero-initialised): `.text` -60 and `.bss`
  +60 is the SAME conventional memory; it would shorten the boot file and buy
  nothing resident.
* **The hot cells' stubs** (`mega_sl_*` 44 x 8, `MEGA_ICW*` 10 x 11): each is
  the minimum for its job in protected mode (a 5-byte selector load and a
  near jump; no writable segment reaches the kernel's data without one). A
  shared stub would need the target found at run time - cycles on every call.

## DEFERRED (estimates, labelled as such)

* **`mega_fd13`'s fallback E9 line as a table** (`.fall`, ~110 bytes of
  `mov al, [x] / call mm_hex2` pairs and separators): a byte list of
  (address, separator) walked by one loop. ~40 bytes `.mega`. Not hot.
* **`mega_fdgo`'s READ/WRITE DATA command as two 3-byte runs** (`[mega_fdc]
  [mega_fdh] [mega_fds]` - needs `mega_fdh`/`mega_fdc` declared in C,H order -
  and `dsk_dpt+4..6` are contiguous): a `lodsb / call mm_fdout / jc / loop`
  helper. ~19 bytes `.mega`. It is per transfer and adds ~13 clocks a command
  byte on a 286 between bytes the 765 paces anyway (fdout spins on RQM), so
  wall time should not move - but it is not provably cycle-neutral; measure on
  86Box before taking.
* **`mega_fdgo`'s seventeen `call mm_fdout / jc .fall` pairs** (5 bytes each):
  a sticky-failure `mm_fdout` that no-ops once it has failed, with ONE `jc`
  per command - ~30 bytes `.mega`, same outcome (any failure still falls
  back), but changes how many polls a dead controller costs; the per-byte path,
  so measure.
* **`mega_hibnc`'s state in a stack frame instead of `mega_hb*`** (13 bytes of
  `.text` data - conventional - plus ~20 bytes of `[mem]` operands): `int 13h`
  keeps every register but AX/flags, so most of the state could live in
  registers and on the stack. ~25 bytes, 13 of them conventional. No row
  reaches it (a program's own int 13h into a high window); needs a harness.
* **Single-caller routines written at the call**: `mega_vrm` (in
  `mega_rmaskf`, ~7), `mega_fdtype` (in `mega_fd13`, ~4), `mega_fdms_wait` and
  `mega_fdtk` (in `mega_fdgo`, ~4 each). ~19 bytes `.mega`; left because
  KERN-MEGA-PLAN and KERN-MEGA-MEMORY-PLAN cite them by name and `mega_fdtk`
  carries MEGADIAG's strip hooks.
* **PM-only state out of conventional memory**: `mega_fd*` (~35 bytes),
  `mega_hb*` (13), `mega_vsw` (4) are only touched in protected mode bar
  `mega_fdbusy`/`mega_rmask` (read by `mega_r286`). A writable alias over
  `.mega`'s scratch (`MEGA_SEL_PSCR`'s shape) would take ~50 bytes of `.text`
  off the 64KB segment, but every access gains a segment override, so the code
  grows by about as much; a net win only if the state moves with a rewrite.
* **MEGAFDMASK's `mega_fdmskon` and `mega_rmaskf`'s option-ROM loop are the
  same gate scan** (a reflecting stub's lines): one routine for both ~35 bytes,
  on the knob build only.
* **`mega_bhex2`'s nibble** by `cmp al, 10 / sbb al, 69h / das`: 1 byte. And
  `mega_bhex2`/`mega_hexb`/`mm_hex2` are three nibble printers (stosb, [di],
  E9); one body with a store hook would not be smaller - noted so nobody
  re-derives it.
* **`MEGA_DIAG` code in the range** (~2.9KB: the floppy strip, the capture
  stubs, `mega_fdhex`, `mega_fdvon`, the hex font) was not reviewed - it is a
  knob build and costs the shipped kernel nothing.

## WANTS THE OWNER

* **Gate the native floppy's E9 lines behind `MEGADIAG=1`**: `.said`'s
  `kern_mega: native floppy A` (~30 bytes code) and `.fall`'s `kern_mega:
  floppy fell back to the BIOS at step ...` (~110 bytes code) in `mega_fd13`,
  plus their two strings in pmcore's range (~75 bytes). ~215 bytes, all
  `.mega` (above 1MB). No row or tool reads them (grepped `tests/`, `tools/`),
  but docs/BOCHS.md blind spot 5 and KERN-MEGA-PLAN 10.3 cite them as the
  shipped build's E9 evidence, so it is a developer-visible change against a
  documented recipe. Not taken.

## WANTS AN ABI BREAK

* **The 8-byte hot cell on kern_mega spends two `nop`s** (`OSAPI_SLOT`'s
  mega arm, 44 cells = 88 bytes of `.text`) because the cell length is ABI.
  A kern_mega-only shorter cell is impossible while one SDK serves every
  kernel; recorded, not proposed.

## CROSS-FILE

* **pmcore** (`MEGA_E9IO`, mega.inc ~4906): `%1 %+ say: call %1 %+ puts /
  ret` is `puts` itself - make `say` a second label on `puts`. 4 bytes `.text`
  + 4 `.mega`.
* **pmcore**: the strings `mega_mfdon`/`mega_mfdfb` (4960-4961) follow the
  owner's answer above.

## ROWS RUN

One pass at the tip `9dee6752`, `python3 tools/os88test.py soak -k megahuge
-k megaxmove -k megavidbig -k megavidtier -k megavidv2 -k megatree -k
mega-uilayer --marty-jobs 1`, all under Bochs: **7 passed, 0 failed** (241.9s).
`megahuge` is the bridge (`mega_hugebf`, `mega_hgfill` both arms), `megaxmove`
an `OSAPI_XMEM_ALLOC` block moved and read back, the three `megavid*` rows the
Video Player's huge and XMEM runs (`mega_hugef`/`mega_hugec`/`mega_xmrunf`,
`mega_hgfill32`), and every row boots `mega.img` off the native floppy path
(`mega_fd13`, `mega_fdgo`, `mm_fd*`, `mega_selbase`) - `mega-uilayer` reads
the apps disk through it for two minutes. `megatree` (0.2s) is a host-side
build check. Not run: `megacurdisk` (86Box's vm/286-mega, the native path
OFF, so it would not reach this code), and nothing here runs the 286 RESET
path that calls the `.text` copy of `MEGA_FDIO` - that change is argued by
reading (TAKEN, *Equivalence*). Also: `make -j1` with the fast tier, 66
passed; `tools/stkbalance.py kernel/mega.inc` 0 unbalanced at base and tip.

## DEFECTS FOUND

None.

## Coordinator's addendum: the 286 reset path, run

This note's equivalence for the `.text` copy of `MEGA_FDIO` was a reading,
"no emulator here runs that path". 86Box does, and the coordinator ran it:
`tests/megafdnat.py` (soak row `megafdnat`) boots `build/mega.img` on
`vm/286-mega` with the native path ON and copies 120KB B: -> A:, so the
`.mega` byte level and `mega_r286`'s real-mode `mega_fdrprobe` run together.
Green at the base and at this branch's merged tip (no fallbacks, 2 to 4
resets during the copy, byte for byte); red with `--break` (8 fallbacks,
the path off, 48 resets). What it still does not reach is `mega_fdrprobe`'s
SEEN branch - a 765 the BIOS reset behind the kernel's back - which nothing
here forces; that branch stays verified by reading.
