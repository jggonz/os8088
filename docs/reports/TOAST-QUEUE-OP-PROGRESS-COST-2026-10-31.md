# Toast queue and operation-wide progress: what each costs (2026-10-31)

Measured on branch `toast-n-progress`, cut from `elendilon` at `c844ccc`, in a
cloud container. Both changes were built as working prototypes so the figures
are bytes off the assembler, not estimates. `tools/kernsize.py --json` on
kern_big and on kern_small (`-DKERN_SMALL`), before and after. The fast tier
passes on the prototype. **Neither change has been driven on the glass.**

## 1. Toasts queue instead of replacing each other (SPEC.md 59)

**The defect.** `toast_show` has one buffer. Whoever calls it last wins. On
kern_mega with RLAPPLY autostarting, `mega.inc` stages `Entered protected
mode` for 146 ticks, and RLAPPLY's `Chipset settings applied` lands while that
toast is still staged or still up, so it overwrites it.

**The prototype** (`kernel/toast.inc`, `kernel/loader.inc`):

- There is **one** queued slot (`toast_qbuf`, `toast_qttl`, `toast_qinst`,
  with the `toast_qwant` flag in `.text`).
- If a toast is up or staged and a **different** raiser calls `toast_show`, the
  new message goes into the queued slot. If the slot is already full, the
  newest message wins.
- The **same** raiser still replaces its own message, as today. So Paint's
  `Encoding...` followed by `Saved X` stays one message and does not queue for
  three seconds.
- `toast_pass` promotes the queued message at the same edge that retires the
  current one (expiry or `toast_kill`). The new strip goes up in the
  composition that takes the old one down: one `menu_draw_bar`, not two.
- A retire (empty string) still kills the current toast, as before. It also
  drops the caller's own queued message, otherwise retiring would make that
  message pop up.
- `toast_owner_gone` drops a closing instance's queued message.
- The loader's `.abort` re-home (SPEC.md 19.2.1) re-homes a queued message as
  well as the current one. RLAPPLY is exactly this case: its entry proc
  returns CF = 1.

| | kern_big | kern_small |
|---|---|---|
| `.text` | +122 | +122 |
| `.bss` | +28 | +28 |
| `.cold` (loader re-home) | +23 | +23 |
| **resident total** | **173** | **173** |

The idle cost is one near call per `ui_task` pass while nothing is up:
`toast_promote` finds `toast_qwant` clear. An inline `cmp`/`je` would remove
that call for about 5 more bytes.

**Open points, priced but not built:**

- **Wait time.** The queued message waits out the current toast's whole
  life. Behind kern_mega's 146-tick toast, RLAPPLY's message would appear
  about 8 s after boot. Capping the current toast's remaining life at
  `TOAST_TICKS` when something queues behind it would be about 12-15 bytes in
  the `.queue` arm.
- **Depth.** A deeper ring would cost about 28 `.bss` bytes per slot, plus
  index code of roughly 30-40 bytes. The collision seen in practice is a pair.

## 2. One progress scale for the whole operation (SPEC.md 12.8)

**The defect.** SPEC.md 12.8.3 keeps the widget's chrome up for the whole
freeze, but re-scales the trough at every `fpg_begin`. A file-manager copy is
`dskw_read_seq` + `dskw_write_seq` once per chunk (`FCP_MAXKB` = 64KB). So a
700KB copy fills the bar about 22 times, and between fills, while the FAT and
directory are written, `fpg_busy` hatches the whole trough over what was just
filled.

**The prototype** (`kernel/fprog.inc`, plus one call site in
`kernel/filecp.inc`):

- **`fpg_opbegin`** takes `DX:CX` = the bytes the whole operation will move,
  counting every phase. For a copy that is twice the file: the read and the
  write both pass `dsk_xfer`'s step. It scales the trough once and sets a new
  `FPG_OP` bit (8) in `[fpg_on]`.
- **`fpg_end`** banks the phase's scale in `[fpg_opt]` as it zeroes
  `[fpg_total]`. `fpg_end` is reached from every `dskw_*_x` epilogue, and it
  still writes no flag.
- **`fpg_begin`** checks `FPG_OP` first. If set, it restores the banked scale
  and continues the same count instead of re-scaling. `fpg_step`, the hot path
  in `dsk_xfer`, is **unchanged**.
- **Between phases**, `[fpg_total]` is 0 as before. So metadata sectors are
  not counted, and the bar does not run ahead of the data. The hatch is drawn
  over the **unlit remainder only** (`fpg_rem`): the filled part stays, and
  the unfilled part is dithered while the FAT and directory are written. This
  is the "dithered remainder" option. When the next data phase starts, the
  remainder goes back to white with one `gfx_fill`.
- **Ending the operation.** `fpg_finish` clears `[fpg_on]` whole at the
  unlock, which also ends the operation, so there is still no bracket to
  forget. `fpg_opend` exists for a caller that ends the operation inside the
  same hold, as the copy loop does once per file.
- **The overdraw repair (`fpg_paint`)** now redraws the lit extent and then
  the hatch, instead of choosing one of them. Without an operation scale,
  `fpg_rem` zeroes the extent when it hatches the whole trough, so nothing is
  drawn twice.
- **The copy loop (`fcp_xfer`)** calls `fpg_opbegin` with twice the source
  file's size and calls `fpg_opend` at its single exit. kern_small already had
  that size from `dskw_stat_x`. kern_big did not, so it gains one
  `dskw_stat_x` before the first chunk. That is one directory lookup, almost
  always served from the directory cache, because `fcp_file1` has just walked
  that path.

| | kern_big | kern_small |
|---|---|---|
| `.text` (fprog) | +100 | +102 |
| `.cold` (the copy site, which is `.cold` on kern_big) | +25 | 0 |
| `FILECP.DRV` on disk (the copy site, which is a module on kern_small) | n/a | +14 |
| **resident total** | **125** | **102** |

Drawing cost per chunk boundary: one `gfx_fill_gray` over the remainder at the
first metadata sector, and one `gfx_fill` to put it back. That is a few
milliseconds against the ~400 ms of a single `int 13h` (PERFORMANCE.md
Part 2).

**Not covered by the prototype:**

- **A folder or multi-select copy** still shows one bar per *file*. A total
  for the whole set needs a sizing walk before the first byte moves. Inside a
  tree, that walk is `int 13h` calls, each about 400 ms on a floppy.
  One-folder multi-select is cheaper, because the Disk window's listing
  already holds the sizes. Not priced.
- **Packages.** See section 4: the case that matters spans more than one
  lock hold, which this prototype cannot.
- **The icon-swap alternative** (a second glyph while metadata is being
  written) would cost about 56 bytes of icon data, plus a two-pass masked
  `icon_draw` at every phase edge. That is more resident bytes and more
  drawing calls than the dithered remainder.

## 3. kern_small takes neither (second commit on the branch)

Both prototypes are now kern_big's alone. `kernel.asm` defines `TOAST_Q` and
`FPG_OPS` under `%ifdef KERN_BIG`, and `toast.inc`, `fprog.inc`, `loader.inc`
and `filecp.inc` keep the original code in the other arm. Checked: kern_small's
`kernel.bin` and `kernel-full.bin` are **byte-identical** to `c844ccc`, and
`FILECP.DRV` is back to 1,821 bytes. kern_big is unchanged by the gating:
`.text` +222, `.bss` +28, `.cold` +48 against the base: the two tables
above added together, **298 resident bytes**. The kern_small columns above
are what it *would* have paid. (The first commit needed a 1-byte PARITY pad
to keep `ui_task` even for `t_hotpar`. The restructured `fpg_paint` arm is
one byte shorter, so the pad is gone.)

## 4. Packages: the Video Player's case spans lock holds

The Video Player loads its hold **one chunk per `W_ONTIMER` callback**
(`vp_ontimer` -> `vp_xstep`, `apps/video/video.asm`), and re-arms the timer
"so the loading has half the machine and the desktop the other half". Each
callback is a separate `gfx_lock` hold. The widget's chrome belongs to a hold
(SPEC.md 12.8.3: `fpg_finish` runs from `gfx_unlock`), so it goes up and comes
down once per chunk. That is the blinking. An operation scale that lives
inside one hold, as `fpg_opbegin` does, cannot fix it: the chrome is gone
before the next chunk starts.

For that, an operation has to outlive the hold, and the chrome with it. Three
things currently assume the opposite, and each has to change:

- **The mouse ISR defers while `[fpg_on]` is set** (`mou_apply`, SPEC.md
  12.8.4), and `fpg_arm` turns on the busy pointer (SPEC.md 7.5.3). Left as
  they are, a parked widget would freeze the arrow and show it busy for the
  whole load, on a desktop that is meant to be live. So a parked widget needs
  `[fpg_on]` = 0 with its state banked elsewhere. The pixels can stay on the
  bar, because the arrow crossing the bar is ordinary save-under work.
- **`menu_bar_text` reserves `FPG_CELLS` only while `[fpg_on]` is set.** A
  parked widget needs the same reservation, or the next recomposition (a
  clock minute, a change of active window) letters over it.
  `menu_furniture`'s repair (`fpg_paint`) also has to fire for a parked
  widget.
- **Foreign modes and fullscreen windows.** `fpg_arm` refuses those, but a
  parked widget's next hold can be inside an fsx bracket: the Video Player
  plays fullscreen, and `vp_xlag` loads behind the play. `fpg_vis` would need
  the same refusals, or a resumed phase draws onto the foreign mode.

Ownership comes with it. The parked operation belongs to the instance that
opened it (`inst_caller`, as toasts do):

- Only that instance's phases resume its scale.
- A phase from anyone else, such as a Disk-window copy during the load, would
  not count. The parked bar would hatch its remainder for the length of that
  other freeze.
- Closing the instance ends the operation (an `fpg_owner_gone` beside
  `toast_owner_gone`).
- A second package asking while one is parked is refused (CF = 1).

### 4.1 Built (third commit): `OSAPI_FS_OP`, one cell

One slot, `OSAPI_FS_OP` at `0x04E9`, as a rare cell (6 bytes).
`DX:CX` = the bytes the operation will move, and `DX:CX` = 0 ends it, so
ending needs no flag at all. `PKG_FMT` stays 10, because the cell is added at
the end of the table (`tools/abi.lock.json` records why). The kernel side is
behind `FPG_PARK`, defined under `KERN_BIG` in `kernel.asm`.

- **The call.** `osapi_fs_op` records the caller (`inst_caller`) as
  `[fpg_pown]` and runs section 2's `fpg_opbegin`. It refuses with CF = 1 if
  another package holds the widget, or if the widget could not arm.
- **Parking.** At every `gfx_unlock`, `fpg_finish` checks `[fpg_pown]`. If a
  package's operation is live, the widget parks instead of coming down:
  - `[fpg_on]` keeps only `FPG_SHOWN | FPG_OP`.
  - The cursor debt is paid and the warm-up count is cleared.
  - The pixels, origin and trough stay on the bar.
- **Parked means not armed.** The three `mou_apply` tests and `fpg_busy` and
  `fpg_arm`'s entry ask the ARMED bit (`FPG_ARMED`, a `test` the same length
  as the `cmp` it replaces). So the arrow moves normally and the busy pointer
  is off while parked.
- **Showing.** `fpg_vis` accepts `1 | FPG_OP`. A parked widget keeps its menu
  cells, `menu_furniture` repaints it, and a toast still outranks it.
- **Unparking.** `fpg_arm` arms a parked widget where it stands, without
  resetting the origin or the trough. Every refusal still applies: the
  splash, a bracket, another task's lock, a fullscreen window.
- **Only the owner's reads count.** `fpg_begin` resumes the scale only for a
  phase whose `inst_caller` is the owner. Anyone else's phase leaves the
  trough unscaled, so its remainder hatches. A kernel `fpg_opbegin` (the copy
  loop) refuses while a package owns the widget.
- **Ending.** The end call, or the owner closing (a check in
  `toast_owner_gone`, which both teardown sites already call), only disowns
  the widget. The next `gfx_unlock` takes it down.

| MEASURED | kern_big | kern_small |
|---|---|---|
| the `OSAPI_FS_OP` cell | +6 | +6 (an `OSAPI_XREFUSE`) |
| parking, `.text`, incl. a 1-byte PARITY pad for `ui_task` | +122 | 0 |
| `.bss` | 0 | 0 |
| **total on top of sections 1-2** | **128** | **6** |

So kern_big is **.text +350, .bss +28, .cold +48 = 426 bytes** with all three
changes, and kern_small is +6. kern_big's image rung crosses
(91 -> 92 steps; 3,072 bytes of `KERN_BUDGET` spare). The unparked variant is
one line: comment out `%define FPG_PARK 1` in `kernel.asm`. That gives
section 2 plus a refusing cell, `.text` +106 over the first commit's figure
(+100 for section 2, +6 for the cell), and no rung crossed.

**No package calls it yet.** The Video Player is the consumer, with
`OSAPI_FS_OP` at the start of the hold load and its end (or the close). Two
things come before that. First, a shipped package calling `0x04E9` is a new
Wire API level (SPEC.md 92 and `tools/os88wire.py`'s level table). Second,
the parked widget has to be seen on a slow machine, which this session has
not done.

## What was not done

- Neither change was exercised in an emulator. The progress widget needs
  MartyPC to be seen at all, because QEMU finishes the copy inside one frame.
  The kern_mega + RLAPPLY case needs Bochs or 86Box (docs/BOCHS.md,
  docs/86BOX.md).
- No SPEC.md section was rewritten. If any of this is kept, SPEC.md 59.3,
  12.8.3, 12.8.4 and the API table change before the code does.
- The soak rows that cover these are `toastbar`, `toastfpg`, `fpgcold`,
  `fpgnotch`, `curdisk` and `rltune`. None of them has been run.
