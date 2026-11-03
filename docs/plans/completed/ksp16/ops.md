# ksp16 agent "ops" - toasts, the file-progress widget, `OSAPI_FS_OP`, the counting hunks, `fsx.inc`

Kernel size pass 16 (`docs/plans/completed/ksp16/BRIEF.md`, row `ops`):
`kernel/toast.inc`, `kernel/fprog.inc` (`fpg_*`, `osapi_fs_op`), the counting
hunks the toast/progress commits put into other files (`dsk_xfer`,
`fcp_xfer`, `ld_start`, `dskw_read_at_x`; `ui_grow` turned out to carry no
counting hunk, and `cmz_*` is CTRL.DRV/DISKOPS.DRV, not resident), and
`kernel/fsx.inc`. Added mid-pass by the coordinator: **`make mega` failed its
own `tests/megaseg.txt` gate at the base**, and `fsx.inc` was the cause.
Branch `ksp16-ops`, base `714e9d64` (the brief's commit on `23791571`).
Figures are ASSEMBLED bytes, `tools/kernsize.py --json` absolutes (kern_mega
from its own `kernsize[mega]` lines); resident = `.text` + `.bss` + `.cold` +
`.lowbss` + `.vgabuf` + `.vga`, plus `.mega` on kern_mega. Clocks are 8088
unless a line says 286.

## BASE AND TIP

| | base | tip | delta |
|---|---:|---:|---:|
| **kern_big resident** | 92,136 | **92,062** | **-74** |
| kern_big `.text` / `.bss` / `.cold` | 41,289 / 5,418 / 37,703 | 41,223 / 5,421 / 37,692 | -66 / +3 / -11 |
| kern_big `.lowbss` / `.vgabuf` / `.vga` / `.boot2` / `.ovl` / `KERN_SIZE` | 5,358 / 336 / 2,032 / 6,456 / 5,119 / 97,792 | the same | 0 |
| **kern_small resident** | 57,984 | **57,984** | **0** - every `kernsize --json` figure identical (TOAST_Q, FPG_OPS and FPG_PARK are kern_big's; each changed site keeps the old lines in its `%else`) |
| **kern_emu resident** | 92,392 | **92,318** | **-74** (the same split as kern_big) |
| **kern_mega resident** (with `.mega`) | 125,598 | **125,529** | **-69** |
| kern_mega `.text` / `.bss` / `.cold` / `.mega` | 46,592 / 5,456 / 45,997 / 21,648 | 46,467 / 5,459 / 45,986 / 21,712 | -125 / +3 / -11 / +64 |
| kern_mega segment overhead (`tools/os88megaseg.py`) | **3,309 against 3,231 allowed: `make mega` FAILED** | 3,250 against 3,250 | -59; gate green |
| kern_mega footprint after the switch / `.boot2` / `.ovl` | 63,488 / 6,739 / 5,118 | the same | 0 (no rung) |

The `.bss` +3 is not growth: `toast_ttl` and `toast_inst` moved out of
`.text` into the staged message's `.bss` record (item 1). `.mega` +64 is +49
of moved protected-mode code and +15 of the rung's alignment before `rms_lo`.
No rung moved on any kernel; `.boot2`, `.ovl`, `.lowbss` and every stack are
untouched.

## TAKEN

### 0. kern_mega: `tests/megaseg.txt` green the right way (`fsx.inc`) - kern_mega `.text` -59, `.mega` +64; kern_big byte-identical

`1dea2435` put +78 bytes into kern_mega's `.text` and no line into
`tests/megaseg.txt`, so `make mega` failed at the base (3,309 against
3,231). The protected-mode part of that, and of what was beside it, went to
`.mega` (KERN-MEGA-SEGMENTS-PLAN 3.2); the real-mode part stays and the
figure is raised by it, with a line saying so:

* **The speaker ring's close** (`cmp [spk_seg]` .. `mov [snd_ch2mode], ax`,
  25 bytes) runs in PROTECTED mode after `mega_up`: from `fsx_rland`'s `.text`
  to the head of `fsx_rupf` (`.mega`, already far-called next). That puts it
  after `sch_fast_off` instead of before: `sch_fast_off` writes channel 0
  (control word 34h, its count) and reads neither `[spk_seg]` nor
  `[snd_ch2mode]`; the close writes port 61h and channel 2's control word
  (B6h). The PIT's channels are independent and IF = 0 throughout, so the
  end state is the same and nothing between reads the words - the speaker gate
  goes off a few microseconds later, inaudibly. The reason the close exists
  (spk_off reloading channel 0 with the bracket's divisor AFTER the rate went
  off) is untouched: the close still runs before `fsx_restore`'s `spk_off`.
  -25 `.text`.
* **`fsx_rcall`'s banking of SI and BP and its NMI-off / `[mega_drop]` = 2
  stores** (17 bytes, protected mode) into `fsx_rdownf`'s head and tail
  (its only caller is `fsx_rcall`). -17 `.text`, net -7 once item 0's
  `fsx_rcall` head (below) is counted.
* **`fsx_run`'s `test si, FSXF_REAL / jz`** into `fsx_rprepf`'s head
  (`.mega`). The `mega_pm` test stays in `.text`: in real mode `MEGA_SEL_CODE`
  is no segment. -6 `.text`, +7 `.mega`.
* **`fsx_rcall` tail-jumps `wm_pkgcall` when `[fsx_rmode]` = 0**, so
  `fsx_run`'s call site is one `call fsx_rcall` and not a test, two calls and
  a jump: -12 there, +10 at `fsx_rcall`'s head, -2.
* **`fsx_rtick`'s ROM chain on `sch_chstack`** (SPEC.md 8.5) is IRQ0 in the
  REAL-MODE bracket and has to be low. Its two arms now share ONE `pushf / call
  far [mega_old08]`: the task's SP rides in BX across the ROM's IRQ0 (an ISR,
  it keeps every register; `fsx_rtick` is `pusha`'d), and after the call
  `mov sp, bx / mov byte [sch_chbusy], 0` are no-ops on the plain arm, where
  `[sch_chbusy]` was 0 (a nested entry on that arm is plain too and balances).
  The nested `.tskip` falls into `.eoi` instead of jumping to it. -9 `.text`.
  **CYCLES (286, the only CPU kern_mega runs)**: the swap arm, the common one,
  drops a `jmp short` and turns two memory moves of SP into register moves -
  faster; the plain arm (a ROM service on its own stack) gains `mov sp, bx`
  and one byte store, ~5 clocks.
* `tests/megaseg.txt`: **3,250**, the line saying the +19 that stays is that
  chain.

### 1. The toast queue as two records of one shape (`toast.inc`, `loader.inc`) - kern_big -40 (`.text` -34, `.cold` -9, `.bss` +3)

The queued message (`toast_qbuf`, `toast_qttl`, `toast_qinst`) was laid out
as text, TTL, raiser; the staged one's TTL and raiser were `.text` words
nowhere near `toast_buf`. Now `toast_ttl` and `toast_inst` follow `toast_buf`
in `.bss` in the same order, `TOAST_RTTL` / `TOAST_RINST` / `TOAST_RSIZE` are
the offsets, and an `%error` holds the two records to one shape.

* `toast_show` stores the TTL and raiser through BX into whichever record it
  filled, and the queue path shares `.kill`'s `popf`: -11.
* `toast_promote` - one caller - is written into `toast_pass`'s `.q`, the flag
  taken and cleared by one `xchg` with AL carried past the `popf`, the whole
  record moved by `rep movsw` (`TOAST_RSIZE` even, `%error`-held): -20. **The
  idle pass with nothing up no longer calls out** (pushf, cli, one xchg, popf
  where it was a call, the same test and a ret): faster on `ui_task`'s idle
  pass, which is on the hot list.
* `ld_start`'s abort path re-homes both records through one local body,
  `.rehtoast` (SI = the raiser byte, AL = live): -9 `.cold`. SI and AX are
  dead there: AL alone is the answer, `ld_unreserve` banks AX, and the far
  entry's own path through `ld_alloc` already promises no register.
* **`[toast_inst]` has no initialiser now** (`.bss` is not zeroed, SPEC.md
  12.8.5.1). Every reader asks whether a toast is up or staged first, and a
  toast is staged only by writing it: `toast_show` compares it only under
  `[toast_on]` non-zero, and `toast_owner_gone` and `ld_start` act on a match
  only through `toast_kill` / `[toast_on]`, which say nothing is up. The TTL
  is written before every arm. This is the same argument `toast_qinst` has
  rested on since it was added.

### 2. `FPG_NOWN` = 0FEh frees 0FFh for the kernel owner (`fprog.inc`, `toast.inc`) - kern_big -12

`[fpg_pown]`'s "none" was 0FFh, which is `inst_caller`'s answer for the
kernel, so both `fpg_begin` and `osapi_fs_op` mapped 0FFh to 0FEh before
comparing or storing. With none = `FPG_NOWN` = 0FEh the kernel owner is
inst_caller's own 0FFh and neither maps (-7, -5). No instance index reaches
0FEh. SPEC.md 12.8.8/12.8.9 and four comments that said "owned as 0FEh"
(`compress.inc`, `diskops.inc`, `drivers/hdd/inst.inc`,
`drivers/ramdisk/rdimg.inc` - comments only, no byte) say so.

### 3. `osapi_fs_op`, `fpg_opbegin`, `fpg_opend`, `fpg_finish`, `fpg_arm` - kern_big -18

* `TEXTCALL` (`kernel.asm`, beside `COLDCALL`, with `os88ovlchk`'s far-frame
  rule taught it): `push cs` + a near call for a FAR entry in `.text` called
  from `.text`, a byte under `call KERNEL_SEG:`. `fpg_opbegin`'s call of
  `fpg_begin` and `osapi_fs_op`'s of `fpg_opbegin` (-2).
* `osapi_fs_op`: `inst_caller`'s DH lands in AH by `xchg ax, dx`, the owner in
  AL (the accumulator forms of the load and the compare), the end's `clc`
  goes (`and` clears CF), the held-operation refusal jumps to `.no`'s `stc`
  (-5 with its TEXTCALL; 66 -> 56 with item 2's -5).
* **`fpg_opend` is gone** (-8 `.text`): its one caller, `fcp_xfer`'s `.out`,
  does its `pushf / and / popf` in place (+2 `.cold`). The label `fcp_opend`
  marks it for `tests/fpgop.py`'s trace, which named `fpg_opend` and now
  names it; SPEC.md 12.8.8 says what ends an operation.
* `fpg_finish`'s park: `and word [fpg_on], FPG_SHOWN|FPG_OP` clears
  `[fpg_warm]` with it (adjacent, `%error`-held), and the load of `[fpg_on]`
  moved after the owner test (-5). **Faster on both paths, and the tear path
  is every `gfx_unlock`** - it no longer loads `[fpg_on]` twice.
* `fpg_arm` (FPG_PARK): `[fpg_on]` read once into DL (a documented clobber;
  nothing between writes DL - `wm_fs_vis` keeps all but the flags) for both
  the armed and the parked test (-1).

### 4. `dskw_read_at_x`'s redirected arm - kern_big -4 `.cold`

`ct_fpg_begin` keeps every register, so CX is still `[dwr_cap]` at the
`FSV_READAT` call; the reload is `%ifndef FPG_OPS`.

### Per routine, kern_big, `tools/os88rdiff.py kernel 714e9d64 .`

`toast_show` 141 -> 130, `toast_pass` + `toast_promote` 63 + 54 -> 97,
`toast_ttl`/`toast_inst` -3 `.text` (+3 `.bss`), `fpg_begin` 112 -> 105,
`fpg_arm` 106 -> 105, `fpg_opbegin` 37 -> 36, `osapi_fs_op` 66 -> 56,
`fpg_opend` 8 -> gone, `fpg_finish` 74 -> 69, `ld_start` 327 -> 318,
`dskw_read_at_x` 255 -> 251, `fcp_xfer` 202 -> 195 + `fcp_opend` 9 (+2).
`disk.inc`'s `align 2` after `dsk_img16` is not in the spans these move.

## HOT PATHS AND PARITY

* `dsk_xfer`'s per-sector and per-run paths: **not touched**.
* `ui_task`'s idle pass (toast_pass): faster (item 1). `gfx_unlock`'s
  `fpg_finish` tear: faster (item 3). IRQ0 on kern_big: untouched; kern_mega's
  real-mode `fsx_rtick`: item 0.
* `t_hotpar` green on both gated kernels. **No parity pad added.** `ui_task`
  is on its measured even parity (two intermediate builds had it odd, and the
  last bytes taken were chosen to put it back). `fpg_step`, `dsk_xfer`'s
  per-sector caller and not on `t_hotpar`'s list, is back on its base's EVEN
  address (-8). Two routines t_hotpar does not gate now sit at an ODD shift
  from the base: `fpg_busy` (-7, once per transfer while unarmed) and
  `fpg_finish` (-27, per `gfx_unlock`); neither matters on an 8088.

## VERIFIED

* **The toast queue had no row at all** - `grep` finds no test that ever
  queues a message. A scratch MartyPC harness (`os8088_5150_cga_gla`, not
  committed) CALLS `toast_show` on the paused machine (parked at
  `toast_pass`'s entry on the UI task, a near return into it, `snd_inst`
  stamped for a second raiser) and checked, at the tip: a kernel message
  staged and armed into the main record (text, TTL 400h, raiser FFh); a
  second raiser's message queued with its TTL and raiser and the up one
  untouched (a control byte dropped from it, as `toast_show` does); the first
  raiser replacing its own without touching the queue; the up one expired by
  winding `[toast_die]` back and the queued RECORD promoted whole (text, TTL
  300h, raiser 3, `[toast_qwant]` 0, `[toast_on]` 1); and raiser 3's empty
  string retiring it. **PASS.** It is the obvious row to register if the
  coordinator wants one (WANTS THE OWNER is not needed for that).
* `make`, `make small`, `make emu`, `make mega` build at the tip; the fast
  tier (`stkbalance`, `ovlchk`, `hotpar`, `megaseg`) is green in each `make`.

## REFUSED

* **One subroutine for `dsk_xfer`'s two bar-stepping tails** (the driver
  arm's `.drvnotch` and `.count`'s fill/FAT tests): 0 bytes net once the
  read-ahead test it must carry is counted, and putting the `[fpg_total]`
  test inside the call costs the UNARMED int 13h path ~39 clocks a run.
* **`fcp_xfer`'s stat for the scale moved after the first chunk** (the
  READ_SEQ cursor carries the size): ~7 `.cold`, but the first chunk's read
  phase would then not count into the operation, so the bar would end short.
  A behaviour change.
* **`toast_show` storing the TTL and raiser before the copy** (no `push/pop
  cx`, -2): a THIRD raiser's retire would overwrite the queued message's TTL
  and, through the raiser test, drop it. A behaviour change.
* **`inc byte [toast_want]` / `[toast_qwant]` for the `mov byte ..., 1`**
  (-1 each): 256 restages before an arm would wrap it to 0 and lose the
  message.
* **A `fpg_who` helper shared by `fpg_begin` and `osapi_fs_op`** for the
  0FFh -> 0FEh map: 0 net. `FPG_NOWN` (item 2) removes the map instead.
* **`lahf`/`sahf` to carry the zero-byte test past `inst_caller`** in
  `osapi_fs_op`: 0 net.
* **`fsx_run`'s `[fsx_rmode]` test for the region copy into
  `fsx_rcopydf`** (kern_mega, -7 `.text`): on a kern_mega that could not
  switch, `[fsx_rmode]` is the only thing keeping the far call to
  `MEGA_SEL_CODE` from running in real mode.
* **`fsx_run`'s FSXF_REAL answer foot (`.nwhy`) into `.mega`**: it rewrites
  `kent_di`'s frame, so a far frame between costs an offset rewrite and the
  `mega_pm` test has to stay low; ~-4 for a riskier shape. Not taken.
* **`cmp bl, <toast_buf's low byte>`** for `cmp bx, toast_buf` (-1): NASM
  will not take `&` of a label, nor of a cross-section difference.

## WANTS THE OWNER

None. No behaviour change was taken; the one ordering change (item 0's
speaker close after `sch_fast_off` on kern_mega's real-mode bracket) changes
no end state and is argued above.

## WANTS AN ABI BREAK

None. `OSAPI_FS_OP`'s contract is untouched (the kernel owner id is internal).

## CROSS-FILE (for the agent that owns `sched.inc` - gui)

* **`sch_isr`'s ROM chain can take item 0's shape** (`sched.inc` ~2177): its
  registers are already saved there ("unlike SPEC.md 9.10's swap this one
  needs no `cs:` override"), so `mov [sch_chsave], sp` / `mov sp,
  [sch_chsave]` can be `mov bx, sp` / `mov sp, bx` (-4 bytes, and faster on
  IRQ0 - two memory moves of SP become register moves on every tick on a task
  stack), and `.chplain`'s second `pushf / call far` can share the first
  (another ~-3, as `fsx_rtick` did). `[sch_chsave]` then has no reader on any
  kernel: `fsx_rtick` no longer writes it, and STKDIAG does not read it (-2
  `.bss`). Not taken: `sch_isr` is gui's routine.

## ROWS RUN

(once, after the last code commit, at the tip)

`python3 tools/os88test.py soak -j 1 --marty-jobs 1 -k ...`, one pass:
**17 passed, 0 failed, 0 skipped** in 466s.

| row | emulator | what it covers here | result |
|---|---|---|---|
| `toastbar`, `toastfpg` | MartyPC | the staged record (`toast_buf`/`toast_ttl` at their new home), arm, expiry, the widget against a toast | ok, ok |
| `fpgnotch`, `fpgcold`, `fpgop`, `curdisk` | MartyPC | `fpg_begin`/`fpg_arm`/`fpg_finish`, the operation scale and its end (`fcp_opend`) | ok x4 |
| `diskclone`, `czjoin` | MartyPC | the KERNEL-owned operation (owner 0FFh now): the clone, the rejoin | ok, ok |
| `rehomeabort`, `multiseg`, `msegnomem`, `mseglazy` | MartyPC | `ld_start`'s abort and its toast re-home (`.rehtoast`), a package's own operation through the parts library | ok x4 |
| `megacurdisk` | Bochs | kern_mega's widget and cursor | ok |
| `megarmb`, `megarmbref` | Bochs | kern_mega's real-mode bracket: `fsx_rcall`/`fsx_rdownf`/`fsx_rland`/`fsx_rupf`, `fsx_rtick`'s chain, `fsx_rprepf`'s FSXF_REAL test | ok, ok |
| `megarmb286`, `megarmbext` | 86Box (built here: `tools/os88box.py build`) | the same across a 286's RESET drop, and the extended reasons | ok, ok |

The fast tier (`toast`, `hotpar`, `stkbalance`, `ovlchk`, `megaseg`'s
check in `make mega`) is green in every `make`.

## DEFECTS FOUND

1. **`make mega` failed at the base** on its own `tests/megaseg.txt` ratchet:
   `1dea2435` added +78 bytes of kern_mega `.text` with no line. Fixed (item
   0), found by the coordinator's base build.
2. **`docs/INDEX.md` was stale at the base** (the brief's commit added
   `ksp16/BRIEF.md` without regenerating it), so `make` failed `checkdocs` on
   a fresh worktree. Regenerated in this branch's first commit.
3. **The toast queue (SPEC.md 59, `TOAST_Q`) has no registered row**; see
   VERIFIED for the harness that covered it here.
