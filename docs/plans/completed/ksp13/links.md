# ksp13 agent "links" - links and autostart, unpassed

Kernel size pass 13 (the brief is `docs/plans/completed/ksp14/BRIEF.md` on
this branch; the pass was renumbered after it was written). Concept: `*.LNK`
links and autostart from `\AUTOSTRT` with Shift to skip (SPEC.md 26.10) -
`d5d2d211`, `2bff6f77` (the walk into an eleven-sector blob) and the content
the `2cb3c15e` merge brought: `sc_walk`/`sc_open`'s split, `lnk_is`,
`lnk_open`, the hooks in `assoc_run_x`, `ld_pkg_byname` and `fm_dgdrop`,
`sc_lowseg`/`sc_wsp`, `as_boot` in the blob, and CTRL.DRV's `.modc` -
`sc_m_lnk` and, by the coordinator's assignment, the other `sc_m_*`. The desk
agent owns `stik_boot` and the desk-layer hunks, and none of those lines was
touched. Branch `ksp13-links` (cut as `ksp14-links`) from `aedadd25`.

Figures are ASSEMBLED bytes: `tools/kernsize.py --json` absolutes for the
three kernels, resident = `.text` + `.bss` + `.cold` + `.lowbss` +
`.vgabuf`; section and routine figures from `tools/os88rdiff.py` over
listings (`--lst`, base and tip assembled with the shipped KZIP first-pass
flags); CTRL.DRV is the image `os88mod.py` prints, which is the unpacked one.

## BASE AND TIP

| | base `aedadd25` | tip | delta |
|---|---:|---:|---:|
| kern_big resident | 92,757 | 92,727 | **-30** |
| kern_big `.text` / `.bss` / `.cold` | 43,756 / 5,223 / 38,084 | 43,754 / 5,221 / 38,058 | -2 / -2 / -26 |
| kern_big `.boot2`, the real (KZIP) blob | 5,342 of 5,632 | 5,317 of 5,632 | -25 |
| kern_big `.boot2` as `kernsize` prints it | 5,129 | 5,104 | -25 (see DEFECTS: this is the NOKZIP blob) |
| kern_big `KERN_SIZE` | 98,816 | 98,816 | no rung moved (cold 38,084 -> 38,058 of 38,400) |
| kern_emu resident | 93,016 | 92,986 | **-30** |
| kern_emu `.boot2` (`kernsize`) | 5,229 | 5,204 | -25 |
| kern_small resident | 60,260 | 60,260 | 0 - the concept is `OS88_SHORTCUTS`, kern_big's alone |
| CTRL.DRV image | 12,781 | 12,743 | **-38** (`.modc`; still a 13 KB claim and 25 sectors - into the pad) |
| CTRL.DRV settings core (`MODS_SIZE`) | 3,050 | 3,012 | -38 (6 sectors either way: no I/O saved) |

**Against the concept's 164 resident bytes, -30 is 18%.** Every hot routine
(`font_run*`, `gfx_blit1*`, `gfx_blit4*`, `gfx_fill*`, `font_char`,
`sch_isr`, `sch_switch`, the mouse ISRs, `cur_move`, `dsk_xfer*`, `ui_task`,
`kbm_isr`) is identical at an EVEN shift (`os88rdiff --hot`, 31 routines). No
hot path is touched.

## TAKEN

### `sc_walk` tells whose record it is by SP - -12 resident

`sc_walk` took the record's segment through `[sc_wsp]`, a `.bss` word pointing
at a segment WORD (`sc_seg` for a shortcut, whose claim can move under a
mount; `sc_lowseg`, a `.text` `dw LOW_SEG`, for a link's record on the
stack), and both callers stored it. A shortcut-table offset is below
`SC_MAX * SC_REC` (1,920) and so below task 0's stack, which starts at
`STK0_BOT` (= `KLOW_SIZE`, 5,358 on kern_big); a link's record is ABOVE SP,
`lnk_open` calling the walk from beneath it. So the walk now reads
`[sc_seg]`, compares DI with SP and takes SS when DI is above: +4 in the walk,
-6 in `sc_open`, -6 in `lnk_open`, -2 `.text`, -2 `.bss`. kernel.asm's foot
asserts the bound (`SC_MAX * SC_REC > STK0_BOT` is an `%error`; inverted on
purpose, it fires). Every context that walks is on task 0's stack: `sc_open`
from `desk_open` (the UI task) and `stik_boot` (kmain, `SP = STK0_TOP`), and
`lnk_open` from `assoc_run_x`, which is the UI task's by contract - and
`lnk_open` refuses below `STK0_BOT + LNK_SPMIN` before it reaches the walk at
all. Equivalence is by that reading; `desksc`, `stik*` and `autostart` are
the rows that reach both arms.

### `lnk_open` reads its record through SS:BP - -1

`mov bp, bx` after the read (a `mov` writes no flag, so it sits between the
call and its `jc`), then `[bp+...]`, which is SS by default: the signature
compare is the same length, the version compare, the volume load and the
terminator store each lose their `es:` prefix, against the 2-byte `mov`.
`lnk_open` is 87 -> 80: -6 is the `sc_wsp` store counted above, -1 is this.
(A `pop ax` for the signature saved one more and was not taken: REFUSED.)

### `fm_dgdrop`'s Alt arm - -15

The volume load moves above the `jz` (no `mov` writes a flag), so the Alt
arm and the plain one share it; the Alt arm then ends in the desk drop's own
`mov byte [fcp_cbop], FCP_NONE` / `jmp sc_modcall`, reached by a short jump
(`.dsc`), instead of a copy of both. -9 of code, and the arm is short enough
that `jz .desk` and the first `jz .out` - relaxed since the Alt arm went in -
are short again: -6 more. Two lines of the desk drop (`.desk`, 26.8.4) moved:
the `mov ah` now comes before the disarm, which is the shared tail's label.
Registers into `sc_modcall` are identical on both arms (AL, AH, DX; CX/DX the
release point on the desk arm).

### `assoc_run_x`'s link exit - -2

`.lnk: jmp short .out` was a trampoline because `jnc .out` cannot reach; the
`jmp short .out` that ends `.handover` two bytes above it is the same
instruction, so `.lnk` now labels that.

### CTRL.DRV: `sc_m_go`, `sc_m_wpath`, `sc_m_kind` - -38 image

`sc_m_lnk` was written as "sc_m_new's path code minus the picture", and it
was: bank, stand in the entry's folder, `dsk_path` one byte before the
field, `\` and the name. `sc_m_go` (the bank and the walk, 18) and
`sc_m_wpath` (the path and the name, 33) are that code once; `sc_m_new` calls
`sc_m_offz` between them because the mount may move its claim, `sc_m_lnk`
points ES:DI into its own image. `sc_m_kind` is the `[fcp_cbtype]` bound both
wrote. `sc_m_lnk` composes the link's NAME (ES = DS) before its record
(ES = CS), so the write finds ES = CS as it stands. `sc_m_dfind` compares
with `cmpsb` (and a `cld`, the module promising nothing about DF). -31, -4,
-3. The image stays a 13 KB claim and the core 6 sectors, so this buys disk
and nothing else; it is recorded because the brief puts CTRL.DRV second.

### `as_boot` - -25 blob

The folder's cluster lives in DX for the walk (nothing in the loop writes DX:
`fcp_goto` writes AX, `dsk_find` AX and CX, `lnk_is` SI and DI, and
`assoc_run_x` preserves every register), so `as_dir` and its two `cs:`
accesses go (-8). The three root/folder walks share `.go` (-6); the two name
copies share `.cp`, which reads ES:SI (-3); Shift is `int 16h` AH=02, the
ROM's read of the same `0040:0017` byte, in place of a DS swap (-5). The
find buffer stays IN THE BLOB: on the stack it was 24 bytes more, and it was
refused for what it costs `LNK_SPMIN` (REFUSED).

## REFUSED

* **The blob back to TEN sectors.** `kernsize` reported the blob at 5,129
  and 5,085 after the first cut, which reads as 35 bytes under 5,120; it was
  built (`BOOT2_SECS` 11 -> 10, `KSIG_OFF` 5120 -> 5632 in the Makefile and
  boot2.asm) and the first-pass guard refused it at **5,298**. The shipped
  blob is assembled with `-DKZIP` and carries the kernel's LZ4 decoder,
  which `kernsize` does not assemble (DEFECTS). Ten sectors needs 178 more
  bytes off the blob, most of them not this concept's; reverted whole.
* **`as_boot`'s find buffer on the stack**: -19 more of blob. Built, then
  taken out before commit. At autostart `as_boot` is on task 0's stack, and
  `lnk_open` follows a link only with `LNK_SPMIN` (320) of it left. An
  estimate puts a link that names a link, at autostart, within a few dozen
  bytes of that floor on the base, so 24 more bytes could turn a followed
  chain into a silent nothing - a refusal-path change. Not measured; not
  worth the measurement for 19 bytes of a freed blob.
* **`lnk_open` popping the signature**: -1 resident. `tools/stkbalance.py`
  reads `sub sp, LNK_SIZE` (a symbol) as nothing and the `pop` as a word, so
  the routine reads as unbalanced; spelling the sizes as literals with an
  assertion would pass the gate and read worse. One byte.
* **Folding `lnk_is` into `lnk_open`'s head** (so `assoc_run_x`'s hook is one
  call): 0. The hook loses 5, the head gains 5, and `lnk_is` must stay for
  `ld_pkg_byname` and `as_boot`.
* **`lnk_is`'s `or si, si`** (ZF = 0 for "no extension"): `assoc_ext_of`'s
  CF = 1 path arrives with ZF = 1 (`jz .none`), and pointing DI at a constant
  first costs as much as it saves, while `assoc_extb` is stale on that path
  and can hold `LNK`.
* **`as_boot`'s find buffer over `assoc_doc`**: the locator is 16 bytes and
  the answer 24, so it would run over `assoc_dead` and `assoc_pwin` into
  whatever `.bss` follows - the assoc agent's layout.
* **`sc_lowseg` aimed at an existing `dw LOW_SEG`** (-2): the only other one
  is `dsk_dbg_tsg`, which assembles only under `DISK_COUNTERS`. Moot now.
* **`sc_m_same` with `cmpsb`**: the two `add`s that offset SI and DI cost
  what the loop saves.

## WANTS THE OWNER

* **Following a link through CTRL.DRV** - ~-65 resident on kern_big and
  kern_emu (ESTIMATED: `lnk_open`'s 80 out, a modcall at `assoc_run_x`'s hook
  and a far entry for `sc_walk` in). A double-click on a `.LNK`, an
  `OSAPI_PKG_START` of one and autostart would read the settings core first:
  the system disk is then needed to follow a link where today it is not, and
  a machine without it in A: answers `Needs Sys Disk A:`. Autostart runs with
  the boot disk in, so it alone would not change; the Disk window's
  double-click would, and costs a core read (6 sectors) each time.
* **...and a desktop shortcut's open with it** - ~-220 resident (ESTIMATED:
  `sc_walk` 150, `sc_open` 13 and `lnk_open` 80 out, three modcalls and the
  far entries in). Every shortcut double-click would read the core and need
  the system disk. Bigger, and a change to SPEC.md 26.8.5's promise rather
  than to this concept's; recorded so the question is asked once.
* **`\AUTOSTRT` on the 360KB system disk.** SPEC.md 26.10.5 left it off for
  room, the eleventh blob sector having taken `KERNEL.SYS` over a cluster.
  Nothing here gave the cluster back (the blob is still eleven), so this is
  unchanged - noted only because the blob's 10-sector question is what would
  answer it.

## WANTS AN ABI BREAK

None. Nothing in this concept is on `main`, and nothing here changed an
interface: `SC_OP_LNK` and the `CPE_SC` door keep their registers, and the
`.LNK` format is untouched.

## CROSS-FILE

* `kernel/files.inc` `fm_dgdrop`'s `.desk` (26.8.4, wkfs's lines): two lines
  reordered and a label added to make the shared tail above. If wkfs edits
  `.desk`, keep `.dsc` on the disarm.
* `kernel/assoc.inc` `assoc_run_x`'s `.handover` (assoc's lines): a label,
  `.lnk`, on its closing `jmp short .out`; the link hunk below it is gone.
* `kernel/kernel.asm`'s foot (wkcore's): the new `SC_MAX * SC_REC >
  STK0_BOT` guard, beside guard 3's task-0 stack checks.
* **For wkcore: the blob is 315 bytes short of eleven sectors and 197 over
  ten**, at 5,317 with the decoder. Ten is a sector off `KERNEL.SYS` and stage
  1's read, `KSIG_OFF` back to 5632 (file sector 21 still), and probably the
  cluster SPEC.md 26.10.5 wants for the 360KB disk's `\AUTOSTRT`.
* `tools/kernsize.py` (DEFECTS below) is nobody's file this pass.

## ROWS RUN

One pass after the last code commit (`9ff60439`), MartyPC `--marty-jobs 1`
on the GLaBIOS twins, `rltune` on QEMU (its AT path):

| row | result | reaches |
|---|---|---|
| `autostart` | ok 35.4s | `as_boot` (a link at boot, a root `CALC.LNK`, the Shift skip through `int 16h`, a non-`.LNK` ignored), `lnk_open` -> `sc_walk` above SP, the Alt-drop (`fm_dgdrop` -> `sc_m_lnk`, `sc_m_go`, `sc_m_wpath`) |
| `rltune` | ok 82.8s | Apply on boot's link, and a reboot following it |
| `doslnk`, `dosopen` | ok 48.0s, 140.4s | `assoc_run_x`'s hook falling through for the DOS box's own `.LNK` |
| `stikboot`, `stikmiss`, `stikshort`, `stikregister`, `stikmove` | ok 18.0 / 30.1 / 11.6 / 56.2 / 33.1s | `sc_open` -> `sc_walk` below SP from `stik_boot`, `sc_m_add`/`sc_m_dfind` (the `cmpsb`), `sc_m_pdel` |
| `desksc` | **FAIL** 66.6s, leg G - **PRE-EXISTING** | legs A-F pass: a desktop drop (`fm_dgdrop`'s `.desk` -> the shared `.dsc`, `sc_m_new` through `sc_m_go`/`sc_m_wpath`/`sc_m_kind`: whole path, kind, header caption, the Cut disarmed), a shortcut's open, a move, a reboot, a Delete |

`desksc` leg G waits for a Disk window whose folder cluster is **4**; the
window that opened is `MEDIA`, `(1, 5, 1)`, and `\MEDIA` IS cluster 5 on
`build/apps360.img` (read off its root directory). The cluster is a constant
in the test that the apps disk's layout has moved past. `tools/os88bisect.py
classify desksc --base aedadd25 -n 2`: 2/2 failed at the tip and 2/2 at the
base, same leg - not this branch's. Not fixed (a test constant, nobody's
concept).

Not run: `stikmovecga`, `stikhiber`, `stikswap`, `stiknodesk`, `stikanchor`,
`stiksmall`, `stik` - the same `sc_open`/`stik_boot` paths as the five above
on other adapters or with the desk layer's own subjects, which this branch did
not touch. kern_small is byte-identical, so its rows cannot see this.

## DEFECTS FOUND

* **`kernsize`'s `boot2` is not the blob that ships.** It assembles the
  kernel without `-DKZIP`, so it leaves out boot2.asm's LZ4 decoder and the
  packed read: 5,129 against the 5,342 the KZIP first pass (the one the
  `BOOT2_SIZE > BOOT2_PAD` guard binds) assembles on the base. A `boot2` of
  5,085 reads as "fits ten sectors" and does not. Not fixed (not this
  agent's file); found by building the ten-sector blob and watching the guard
  refuse it.
* **`tools/os88rdiff.py kernel <commit>` cannot assemble a commit here**:
  `kernel/vga12.inc` includes `drivers/thmpal/thmrows.inc` by a path relative
  to the tree root, and the exported tree is assembled from a directory that
  has no `drivers/`. `--lst` over two listings built from two worktrees is
  the workaround used throughout. Not fixed.
