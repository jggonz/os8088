# The global directory listing, and whether it has to be `.lowbss`

**OPEN, nothing built.** Measured on the tree at `49a4eec9`. It starts from a
size question — *what is `DSK_NENT` buying us?* — and the answer turns out to
be that the listing is not the file manager's cache at all, and that the
mechanism for moving it is **already in the kernel and already used**.

## 1. What it is, and what it costs

`disk_dir` is **the current directory as a kernel-wide snapshot**, not a
window's cache. `disk_mount` builds it; the per-window claims are COPIES of
it, not the other way round. `disk.inc`'s own comment names the invariant it
buys: *"`disk_dir` is ALWAYS exactly a mount snapshot, with no third staleness
rule anywhere in the kernel."*

| | `kern_big` | `kern_small` |
|---|---|---|
| `disk_dir` — `DSK_NENT` × `DSK_DE_STRIDE` | 64 × 24 = **1,536** | 32 × 24 = 768 |
| `dsk_icoix` — one reference byte per entry | **64** | 32 |
| | **1,600** | 800 |

`.lowbss` is **7,966** of an 8,704 rung with 226 left, and it sits BELOW
`HEAP_SEG`, so every byte there comes off the heap and off the DOS arena.

## 2. THE CENSUS, and three of the scary ones are not consumers

`ui.inc`, `assoc.inc` and `snd.inc` all mention `dsk_get_dir` and **none of
them reads the listing**: each says *"the `dsk_get_dir` idiom"* while
describing its own staging loop. `ui.inc`'s launch path is
`ldf_ld_run_name`, whose own comment is *"BY NAME: no listing to build and
none to refresh."* That was the first thing this study got wrong.

**And `OSAPI_FILE_FIND` does not read it either.** `api_file_find` →
`dsk_find_x`, which re-walks the directory off `[dsk_cwd]` through the sector
cache (SPEC.md 19.7.1). So **no package anywhere depends on the global
listing** and this is wholly a kernel-internal question.

The real readers:

| where | what it wants |
|---|---|
| `disk.inc` | builds it, and reads it back in the icon harvest's second pass |
| `files.inc` | Disk windows — and they already hold their own copies |
| `fdlg.inc` | the modal file dialog. `fdlg_rows` is `mov ax,[disk_nfiles]`, plus ~6 read sites, and it has NO store of its own |
| `loader.inc` | launch by directory INDEX |
| `hiber.inc` | two sites, and both want only *first cluster, by name* |
| `diskw.inc` | publishes the empty listing |

## 3. THE FINDING: the mount is ALREADY two-mode

This is what makes the whole thing tractable, and it is built, shipped and in
daily use.

`disk_mount` step 3 is behind `cmp byte [dsk_quiet], 0`:

  * **quiet** — the scan, the sort and the per-file icon harvest are all
    skipped. `[disk_nfiles]` goes to **0** (deliberately, because the buffer
    still holds the PREVIOUS volume's entries and a stale count is the one
    thing that could make a reader believe they are this volume's),
    `[dsk_lstale]` = 1 is the debt, `[dsk_mntok]` = 1 because the volume is
    still validated;
  * **loud** — the listing is built into `[dsk_dseg]:[dsk_doff]`, capped at
    `[dsk_nmax]`, and `[dsk_lstale]` is cleared.

`dsk_relist_x` pays the debt with a loud remount and is idempotent, which is
what lets the copy engine call it on every exit path.

**So "give the mount a target" is not a new mechanism.** The loud/quiet split
already decides WHETHER a listing is built; `[dsk_dseg]`/`[dsk_doff]`/
`[dsk_nmax]` already decide WHERE. Those three words were a per-VOLUME
destination until SPEC.md 22.6 retired the donated claim this cycle — the
change is to make them a per-CALLER destination instead, which is the same
indirection pointed at a different question. Every reader stays
byte-identical; `dsk_get_dir_x` already stages through them.

## 4. THE DESTINATION TABLE

`disk_mount_x` has exactly **three** call sites — `osapi_vol_mount_x`,
`dsk_chdir_x` and `dskw_remount_x` — so the question is really about who
drives `dsk_chdir_x`.

**Already quiet, and therefore need nothing:**

| caller | why it is quiet |
|---|---|
| `inst_vol_enter` — EVERY package file call | *"the sort and one icon-harvest read per file are all bought for nothing here"* |
| `drv_vol_back`, `assoc_back` | same terms |

**Loud today, and what each would name:**

| caller | destination |
|---|---|
| `files.inc` Disk-window navigation | that window's `FS_VSEG` claim — it already holds exactly this |
| `fdlg.inc` × 6 | **its own claim.** Modal and temporal, so it can afford one for its lifetime; it is also the heaviest reader, so it wants the full shape |
| `dskw_remount_x` / `dsk_relist_x` | whoever is standing — the acting window. With no window standing there is no consumer, so the debt need not be paid at all |
| `hiber.inc` × 2 | **none — BUILT (wave 1).** Both wanted first-cluster-by-name; `dskw_stat_x` answers the cluster in BX and the size in DX:CX off a directory walk. It was reached through `COLD_SEG:hbk_bp` already, so re-pointing it was the same shape at the same cost — and the DOS-handoff site collapsed two thunked far calls (find-then-stage) into one. `kernel.bin` byte-identical, `hiber.drv` −14. It went first because a resume runs with **no Disk window in existence**, so it is the one reader no window's cache could ever serve |
| `osapi_vol_mount_x` | **none obviously needed.** A driver mounts a volume; the desktop draws a zone; the listing is built when a window opens it |
| `osapi_file_goto` | **none — see §8** |
| `dskw_chdir_dl` | the write path's own |

`loader.inc` is not a mount caller at all, but it is a listing READER:
`ld_run_body_x` takes AX = a directory index, bounds it against
`[disk_nfiles]`, stages the entry, checks `[si+LD_DE_TYPE] == 1`, banks the
display name (SPEC.md 20.2's "which file did I come from") and takes the size
and first cluster. **Its index is an index into the listing the acting window
is showing** — `ld_pending` is a Disk window's row + 1 — so the window's own
cache is the right source and this is one of the easier conversions, not a
blocker. The by-name entry beside it reads no listing at all.

## 5. `.lowbss` -> a heap claim is NEUTRAL, and that is the owner's correction

A pinned claim is the same memory: `.lowbss` comes off the heap already. The
move buys something only if the block can **go away** — purgeable, or
transient with the consumer that asked for it.

Purgeable is not free the way the icon store is. `MEM_P_ICO` sits at
`MEM_PG_TRIV` because *"losing it costs a REDRAW, not a read"*; losing the
LISTING costs a **re-mount**, which is real `int 13h` work, so it wants a
higher rank and every reader has to survive it vanishing — which, usefully, is
the same predicate `[dsk_lstale]` already expresses.

**Transient-with-the-consumer is the shape this study prefers**, because §4
says every loud caller either has a window, can own a claim, or should not be
loud.

## 6. THE `.ovlw` CEILING — MEASURED, and it is not the blocker

`.ovlw` — the boot overlay's window half — is **loaded onto this region** and
`kernel.asm:7559` guards it:

```
roundup(OVLW_SIZE, 512)  <=  FAT_PARA*16 + DSK_WIN_BYTES
```

Both sides, both builds, measured on this tree (`-DKERNSIZE`, and
`DSK_WIN_BYTES` is `dsk_secbuf` 512 + `disk_dir` + `dsk_icoix` + `DSK_OVLPAD`):

| | `.ovlw` | rounded | FAT window | `DSK_WIN_BYTES` | ceiling | spare |
|---|---|---|---|---|---|---|
| `kern_big` | 5,074 | 5,120 | 4,608 | 2,112 | 6,720 | 1,600 |
| `kern_small` | 1,900 | 2,048 | 1,024 | 1,312 | 2,336 | 288 |

Take `disk_dir` and `dsk_icoix` out and `DSK_WIN_BYTES` is `dsk_secbuf`
alone:

| | new ceiling | rounded `.ovlw` | verdict |
|---|---|---|---|
| `kern_big` | **5,120** | 5,120 | **passes — by exactly nothing** |
| `kern_small` | 1,536 | 2,048 | **fails by 512** |

**This revises the section's own conclusion.** It said the `.ovlw` bodies
*"have to move into `.ovl` FIRST, and this is not optional."* They do not, and
it is:

* `kern_big` passes **today**, with no overlay work at all. The full 1,600
  bytes are reachable and the guard holds at zero spare — which is loud, not
  silent: it is a `%error` at assembly, and `kern_small` has shipped at 288
  spare (148 real bytes of `.ovlw` growth) for a cycle already.
* `kern_small` has a landing pad built for exactly this. `DSK_OVLPAD` is 0
  and `dskwin.inc:215` says why it is kept: *"the NEXT thing that joins
  `.ovlw` needs somewhere to be told about."* Set it to 512 and the region is
  1,024 + 1,024 = 2,048, which is the rounded `.ovlw` exactly.

So on `kern_small` the deletion buys **288 bytes, not 800** — and that is
`dskwin.inc`'s own standing finding restated: *"kern_small saves less than
kern_big … because its boot overlay, not its listing, is what sizes this
region."* The other 512 is real and is held by the overlay, not by the
listing.

**`.ovlw` → `.ovl` is therefore not a prerequisite, it is the SECOND HALF OF
THE PRIZE**, and it is what turns `kern_small`'s 288 into 800 and
`kern_big`'s zero spare into a rung of it. Priced:

| | `.ovl` now | capacity (`BOOT2_PAD` − `OVL_AT`) | free | needs to move |
|---|---|---|---|---|
| `kern_big` | 1,511 | 1,984 | 473 | 466, for one rung of spare |
| `kern_small` | 1,333 | 1,984 | 651 | 364, to retire the pad |

Both fit — `kern_big` by seven bytes, which is not a margin anybody should
build on. `BOOT2_SECS` 9 → 10 adds 512 to both and costs **no resident
byte**: the blob is `mem_unblob`'d at the end of `kmain`. What it costs is
`KSIG_OFF`, the boot canary's file sector, which must cross a head on all
four geometries and whose legal band moves with the blob length (the Makefile
tabulates it for 8 and 9). `tests/unit/t_canary.py` is the gate, and **the
owner rates re-deriving it low**: SPEC.md 18.93.1 is a fallback for a BIOS
class nobody has yet produced, so it is arithmetic rather than risk.
## 7. What it buys

**1,600 bytes of `.lowbss` on `kern_big`** — rungs off `LOW_PARA`, and every
one of them is heap AND DOS arena byte for byte. On `kern_small` it is
**288 at wave 5 and 800 once wave 6 lands**, and §6 is why the two numbers
are different: there the overlay sizes the region, not the listing.

The rung count is deliberately not quoted here. `.lowbss` stands at 7,966 in
a rung of 8,704, so what 1,600 bytes uncross is a thing to MEASURE at wave 5
and not to predict — and per CLAUDE.md's banner the bytes are the answer
either way.

## 8. A SIDE FINDING, ASKED AND REFUSED

**`OSAPI_FILE_GOTO` builds a listing no package can read** — and it must go
on building it anyway. It is a LOUD `dsk_chdir` (scan, sort, and one
`int 13h` per type-1 file for the icon harvest) where a package reads a
directory with `OSAPI_FILE_FIND`, which re-walks the disk itself;
`inst_vol_enter` afterwards finds the machine already standing there and does
nothing. Every step of that says the harvest is pure cost, and the obvious
conclusion — make the slot quiet — is **wrong**.

**The reader is not a package, it is the next Disk window to act.**
`fmv_sync` (kernel/files.inc:1231) takes its free path only when the acting
window's `(FS_DRV, FS_CWD)` already matches `[disk_drive]`/`[dsk_cwd]` **and
`[dsk_lstale]` is 0**; that third test exists precisely because a quiet mount
leaves the globals naming the right folder with `disk_nfiles` = 0 and the
rebuild owed, and resolving an index against nothing is the silent half of
docs/FIELD-NOTES.md 4. A quiet `OSAPI_FILE_GOTO` raises that debt. So the
package that navigates would not stop paying for the listing — it would hand
the bill to the **Disk window it was launched from**, which is standing in
that very folder in the common case, turning its next action's
compare-and-ret into a full mount.

That is not a saving moved, it is a saving lost: the loud walk happens once
per navigate, the sync's free path is taken on every action after it. **The
slot stays loud**, and the reason it is loud turns out not to be that nobody
asked.

The honest remainder is narrower and is not this plan's: the **icon harvest**
specifically has no reader in a package's navigate, only the scan and sort
do. Splitting the two would want a third mount mode, and §3's finding is that
two is what the mount has; a third is a mechanism to design rather than a
flag to pass.

## 9. WAVES, in the order they bind

0. **`hiber.inc` — BUILT.** §4. The one reader no window's cache could ever
   serve, so it converts to a by-name stat and leaves the list entirely.
1. **`loader.inc` — BUILT.** `loader_run_x` stages the entry itself now, out
   of `[ld_pwin]`'s own cache through `fmv_ld_ent`, and `ld_run_body_x`'s
   step 1 is one compare on the entry's TYPE. It was the last consumer in the
   launch path that needed the global to BE a particular window's directory,
   and both of its neighbours had already stopped — `ui.inc` goes by name
   (SPEC.md 21.4) and `assoc_run_x` reads `FS_DRV`/`FS_CWD` off the poster's
   block (54.9.1). +77 bytes of `.cold`, no rung crossed, and
   `tests/ldcost.py` is the gate.

   **MEASURED, and the headline arm is not the common one** — `os88marty`'s
   `disk()`, 360KB pair, `B:/APPS`, launching `CALC.O88`:

   | | reads | sectors | seeks | transfer |
   |---|---|---|---|---|
   | same window, before / after | 2 / 2 | 12 / 12 | 1 / 1 | 347 / 354 ms |
   | other window, before / after | **10 / 4** | **58 / 13** | **7 / 2** | **1,310 / 471 ms** |

   Six `int 13h` at ~400 ms apiece is ~2.4 guest seconds on the target
   machine. **The same-window arm is PARITY and that is the finding**: §8's
   lesson one layer down. `fmv_sync_x`'s free path was already two compares
   and a `ret` there, so the first build of this wave — a quiet chdir in its
   place — measured **3 reads / 531 ms against 2 / 306**, a change that
   removes a mount and adds a bigger one. `dsk_here_ok` asks whether the
   media CANNOT have changed and a floppy's always can. The shipped arm keeps
   `fmv_sync_x`'s own `(FS_DRV, FS_CWD)` compare in front of the chdir, and
   deliberately does NOT keep its `[dsk_lstale]` test: that one exists
   because its free path leaves the caller resolving an index against the
   global snapshot, and this one does not.
2. **§11 — waves 2 to 5 ARE ONE CHANGE**, and §12 is the objection to the
   end of it. What follows is what they were before that was established,
   kept because the destinations are still right:
   * `files.inc` Disk-window navigation — point the mount at `FS_VSEG`.
   * `fdlg.inc` — the heaviest reader and the only one with no store. Modal
     and temporal, so it can afford a claim for its lifetime. **Open: where
     does that claim come from, and what does the dialog do when refused?**
   * `dskw_remount_x` / `dsk_relist_x` — whoever is standing. **Open: is the
     debt collectable with nobody standing, or does the flag simply stay
     raised until someone loud arrives?**
   * THE DELETION — `disk_dir` and `dsk_icoix` out of `.lowbss`, with
     `DSK_OVLPAD` 512 on `kern_small`. 1,600 bytes on `kern_big`, 288 on
     `kern_small`.
3. **`.ovlw` → `.ovl`**, and `BOOT2_SECS` 9 → 10 if the bodies do not fit
   473 bytes with a margin. Retires `kern_small`'s pad — its remaining 512 —
   and gives `kern_big` back a rung of spare. §6. **This one IS separable**
   and is the only remaining row that is.

## 10. A FOLLOW-ON THIS PLAN DOES NOT COVER: `kern_dos`

Wave 0 converted the kernel's two hibernate sites, and `kerndos/` has **two
more of exactly that shape** — `kdresume.inc:78` and `kdgate.inc:62`, both
`dsk_find_name_x` then `dsk_get_dir_x` for a first cluster and a size. They
are the whole of `kern_dos`'s use of the global listing.

That matters there because `kerndos/kdlayout.inc:151` holds `KD_LOW_KB` at
**3 rather than 2** on this exact ground — *"`disk_dir` STAYS: it looked as
dead as the icons and 96.47's live resume reads it"* — so converting both
would put a **kilobyte of the DOS box's arena** back. `dskwin.inc:163`'s
`DSK_WANT_ICONS` note carries the same claim and would need the same
correction.

It is a separate build with its own budget and its own gates (`kdhdd`,
`kdmouse`), so it is named here rather than absorbed. One thing to check
first: `dskw_stat_x` lives in `.cold`, and `kern_dos` has no `COLD_SEG`.

**And `kdresume.inc` is fragile in a way worth writing down whatever is
decided.** It does a LOUD `disk_mount_x`, then `dsk_chdir_q_x` to the root —
which is quiet, and a quiet chdir leaves `[disk_nfiles]` = 0. The find that
follows works only because `dsk_here_ok` sees it is already standing there
and returns without mounting, so the listing it walks is the LOUD mount's.
Nothing in either comment says the two are coupled.


## 11. WAVE 2 OPENED, AND IT IS NOT A WAVE — **§13 IS THE ANSWER**

Nothing is built here. What follows is what looking at it found, in the order
the facts arrived, because three of the four change the plan.

### 11.1 `files.inc` is ALREADY converted

The only reads of the global left in it are these, and none is the work:

| site | what it is |
|---|---|
| `fmv_load:1176`, `fmv_take:1599` | `mov ax, [disk_nfiles]` → `FS_N`, reading the count the mount it just made produced |
| `fm_measure:1237` | sums `FS_N` entries through `dsk_get_dir_x`, immediately after that same mount |
| `fmv_copy_in`, `fmv_get_icon` | **the `FS_VSEG` = 0 FALLBACK**, which §12 is about |

So "wave 2" is one thing: **where the mount WRITES**.

### 11.2 The mount can write into a claim, and that is MEASURED

`disk_mount` was built for this — `[dsk_dseg]`/`[dsk_doff]` named a
`DVK_DRV` driver's DONATED claim until SPEC.md 22.6 retired it — so
`dsk_put_dir`, `dsk_sortdir`'s `mov es, [dsk_dseg]` and `tools/dsegaudit.py`
are all still there. The question that decides whether it is safe is the
audit's own: *can anything holding this block reach a `mem_claim`, which
COMPACTS on its refusal path?* Asked of every routine in the write window:

```
dsk_synth_x      NO CLAIM REACHABLE      asc_use_x -> asc_seed -> asc_row_take
dsk_put_dir      NO CLAIM REACHABLE                -> ico_add -> ico_scan
dsk_sortdir      NO CLAIM REACHABLE                -> ico_need -> mem_claim_x
dsk_ent_ofs      NO CLAIM REACHABLE
dsk_rd1_x        NO CLAIM REACHABLE
```

**The write window is clean.** The one claimer in the whole mount is
`asc_use_x`, and it is in the icon HARVEST — after the listing is written and
sorted, writing `dsk_icoix` and not the entries — with `ES` already forced to
`LOW_SEG` across it for this exact reason (`disk.inc`, SPEC.md 66.5.10.2).

### 11.3 But `[dsk_dseg]` cannot be put back afterwards, and that is what
makes waves 2 to 5 ONE change

Point it at the window's claim for the mount and there are two endings and no
third:

* **Restore it to `disk_dir` after** — then the global holds the PREVIOUS
  folder while every reader that still goes through it (`dsk_get_dir_x`,
  `dsk_find_name_x`, `dsk_ent_ofs`, `fdlg`'s six sites) believes it holds this
  one. Silently wrong, which is worse than slow.
* **Leave it naming the claim** — then a word in `.bss` names a heap block
  across arbitrary time, and that block is **movable on `kern_big`**
  (`fm_reloc`, files.inc:8516, which fixes `FS_VSEG` and nothing else) and
  **purgeable on `kern_small`** (`MEM_P_VIEW` at `MEM_PG_LOW`, the first rank
  shed). `dsk_dseg_reloc` was the kernel's half of exactly this and SPEC.md
  22.6 deleted it with the donation; its `mem_rr_tab` row went out in this
  same cycle.

So the destination can only move once the global has no readers left — which
is waves 3 and 4 — and the readers can only go once there is a destination.
**They land together or not at all.** A wave 2 taken alone is either a
correctness bug or ~30 bytes of relocation machinery bought back for a benefit
that is not yet there.

And the benefit of wave 2 by itself is small in any case: what it removes is
`fmv_store`'s 1,536-byte `dsk_copy_seg_x`, which at PERFORMANCE.md Set 117.2's
13.3 cycles a byte is **~4.3 ms** on a 4.77 MHz 8088 — against a mount that
has just spent hundreds of milliseconds in `int 13h`. The reason to do it is
the 1,600 bytes at the end, not the copy.

## 12. THE OBJECTION TO THE DELETION — **WITHDRAWN BY §13**

**The global listing is what makes a missing view cache SLOW rather than
FATAL.** `fm_kinit` claims `VIEW_KB` and `jc .nocache`; `fmv_fit` retries at
every store; `fmv_copy_in` and `fmv_get_icon` both fall back to
`[dsk_dseg]`/`[dsk_doff]`. files.inc says so in as many words: *"A refused
claim leaves FS_VSEG 0, which is the documented fallback and not an error: the
window then paints from the global snapshot and costs floppy I/O it would
otherwise have avoided."* Delete the global and there is nowhere to fall back
to.

The two builds differ, and they differ the wrong way round:

* **`kern_big`** — the claim is MOVABLE and not purgeable (it is not in
  `mem_pg_tab` at all). Once a window has it, it keeps it, so the fallback
  fires only on a claim refused at open, which `fmv_fit` already retries at
  every store. Making the claim MANDATORY there is a small change and an
  honest one: PERFORMANCE.md rule 6 says refusal is a normal path, and a Disk
  window that cannot find 4KB refusing to open with a reason is better than
  one that opens and pays floppy I/O for ever.
* **`kern_small`** — the claim is PURGEABLE, at `MEM_PG_LOW`, the FIRST rank
  shed. That is deliberate: `mem_pg_forget`'s `.view` arm hands the owner to
  `fmv_demote`, and memory.inc argues the rank explicitly. Deleting the global
  there turns a purge from *"this window costs floppy I/O now"* into *"this
  window cannot list at all"*, and it would have to become a re-claim that can
  fail at PAINT time.

And `kern_small` is the build where §6 already found the deletion buys **288
bytes and not 800**. So two independent findings now say the same thing:
**this is a `kern_big` plan.** The honest scope is 1,600 bytes on `kern_big`
with the view claim made mandatory there, and `kern_small` left alone — which
also means `DSK_OVLPAD` is not needed and §6's `kern_small` row is moot.

**What to settle before any of it is built**, in order:

1. Is a mandatory view claim acceptable on `kern_big`? It is the whole
   premise, and it is a user-visible refusal.
2. `fdlg` has no store. Does it get a claim, or does it borrow the acting Disk
   window's — and what does a modal dialog do when the claim is refused?
3. `dsk_relist_x`'s debt with nobody standing: is it simply not owed?


## 13. THE SETTLED DESIGN — REPLACE the global, do not MOVE it

§11 and §12 are both answered by one correction from the owner, and it is
that this study had the wrong verb. Everything above was written as *move the
global listing somewhere cheaper*, and the answer is that **there is no global
listing**: a mount writes where its CALLER told it to, and a caller that has
nowhere for it gets none.

That dissolves §12 outright. `MEM_P_VIEW`'s purgeability was only ever
insurance for a shared buffer that had to survive being moved; a store that
belongs to one consumer for as long as that consumer needs it does not need
insurance, it needs an owner. And it dissolves §11.3's circularity, because
the destination is not a standing kernel variable that has to name something
at all times — it is an argument, set for a mount and meaning nothing outside
one.

### 13.1 The end state

* `disk_dir` and `dsk_icoix` **do not exist**. −1,600 bytes of `kern_big`'s
  `.lowbss` and −800 of `kern_small`'s, which is heap AND DOS arena.
* `[dsk_dseg]` / `[dsk_doff]` / `[dsk_nmax]` are the **destination**, an
  input. `[dsk_dseg]` = 0 is *no destination*, and a loud mount with none
  behaves as a quiet one — which is already a state the whole kernel handles.
* **A Disk window** supplies `FS_VSEG`, which it already claims at
  `fm_kinit` and frees at teardown. Per instance, transient with the window.
* **The Standard File dialog** claims one at open and frees it at close.
  Transient with the dialog, and it is the module that today *"needs NO cache
  of its own"* precisely because there was a global to read.
* **Any later consumer** passes its own. The mechanism is the argument, not a
  list of blessed callers.
* Nothing else wants a listing, so nothing else supplies one: `inst_vol_enter`,
  `drv_vol_back`, `assoc_back` and `osapi_vol_mount_x` are already quiet, the
  boot mount has no window, and `dsk_relist_x` with no destination owes
  nothing.

### 13.2 The size, and why it is already minimal

A store is `DSK_NENT * DSK_DE_STRIDE` entries then `DSK_ICOIX_N` reference
bytes — **1,600 bytes on `kern_big`, 800 on `kern_small`** — which is the
view cache's existing layout, so `fmv_iofs`'s arithmetic is shared rather than
duplicated. `DSK_DE_STRIDE` is 24 and `dskwin.inc` refuses below it (name
0..15, type 16, handle 18, size 20..23); the bodies left for the machine-wide
store at SPEC.md 25.9. `VIEW_KB` is **2**, and `mem_claim` rounds to whole KB,
so 1,600 costs 2KB and there is nothing to shave without moving `DSK_NENT` —
a user-visible cap on how many entries a folder shows, and not this plan's to
take.

**What actually shrinks is not the store, it is how often one exists.** Today
the kernel pays 1,600 bytes for ever, on every machine, whether or not
anything is listing. After: nothing, plus 2KB per OPEN Disk window and 2KB
while a dialog is up. A machine sitting on the desktop, or inside a
fullscreen game, pays zero.

### 13.3 What makes it safe, measured

§11.2's audit is the licence: `dsk_synth_x`, `dsk_put_dir`, `dsk_sortdir`,
`dsk_ent_ofs` and `dsk_rd1_x` reach **no claim**, so nothing in the window
where the mount is writing can compact or shed. The one claimer in the mount
is `asc_use_x`, in the icon harvest, after the listing is written, with `ES`
already forced to `LOW_SEG` across it. `tools/dsegaudit.py` is the standing
gate on that and `dsegaudit` is a registered row.

### 13.4 `fmv_sync_x` becomes a QUIET STAND, and that is a speed win of its own

Its six callers were read, and **every one of them wants standing, not a
listing**:

| caller | what it does next |
|---|---|
| `fm_fmt_go` | formats the medium — *"stand on THIS window's volume"* |
| `fm_c_compress` / `fm_c_uncomp` | *"act in OUR folder"* |
| `fm_c_up` | reads `fmv_get_dir` — **the window's cache, already** |
| `fm_edit_commit` | `dskw_mkdir` / rename, by name |
| `.del` | `dskw_rmtree`, by name |
| `loader_run_x`'s `.loud` arm | the `FS_VSEG` = 0 fallback, which §13.1 deletes |

So its `[dsk_lstale]` test goes with the fallback: that test exists because
its free path leaves the caller resolving an INDEX against the global, and
after this nobody does. What that removes is a **loud mount in front of every
delete, rename, New Folder, compress and format** whenever the listing debt
happened to be raised.

### 13.5 The commits

1. **BUILT.** The destination is an input and the Disk window supplies it.
2. **BUILT.** `fdlg` claims its own store at open and frees it at close.
3. **BUILT.** The global is gone: `disk_dir` and `dsk_icoix` are deleted,
   `[dsk_dseg]` = 0 is *nowhere* and makes a loud mount quiet, and
   `DSK_OVLPAD` is 512 again to hold `kern_small`'s overlay ceiling.
   **`.lowbss` −1,600 on `kern_big`** (`LOW_PARA` −1,536, three rungs) and
   −288 on `kern_small`.
4. **BUILT.** `.ovlw` → `.ovl` on `kern_small`: six boot-only bodies moved
   into the blob half through the `OVBCALL` set SPEC.md 2.5.3.2 built for
   exactly this - `sched_init`, `mem_init`, `font_init`, `wm_init`,
   `files_init` and `snd_init`. `.ovlw` **1,900 → 1,342**, which rounds to
   1,536 and fits the region with `DSK_OVLPAD` at nothing, so `kern_small`
   takes the whole 800 after all. `kern_big`'s `kernel.bin` is BYTE-IDENTICAL
   across it.

   `cpu_detect` was the obvious seventh and is REFUSED: `xmem.inc` reaches it
   with a hard-coded `call FAT_SEG:cpu_detect`, so moving the body would
   leave that site calling into the FAT window. The six that moved were
   chosen by asking the question that actually binds - does the block make or
   receive a NEAR call across the two halves - and all six make none.

Two things in 3 came out differently from the design above and are worth the
correction:

* **`MEM_P_VIEW` STAYS PURGEABLE on `kern_small`.** Removing it was in the
  sketch and it costs that build up to 8KB of shed capacity on a 128KB
  machine, which is a worse trade than the thing it was protecting against.
  What the shed needed was not to be impossible but not to be SILENT: a shed
  window has no listing at all now, where it used to fall back to the global,
  so `fmv_demote` leaves it an `FSD_CACHE` debt and raises `[fm_fchk]`. It
  re-claims and re-lists instead of going quietly blank.
* **`FS_DIRTY` grew a second value, and that is what kept the sibling case
  cheap.** `fm_focus` used to spend the debt by copying the global, which was
  free; there is nothing left to copy from, so spending it would have meant a
  MOUNT. `fmv_bcast` knows which windows it just filled, so it says so —
  `FSD_PIXELS` on a sibling whose cache is fresh and only whose pixels are
  old, `FSD_CACHE` where a listing really did go stale. A sibling coming to
  the front now pays **nothing**, where before it paid a copy.

And `fmv_take`'s source moved with it: it reads the ACTING window's claim
(`[fm_vp]`) rather than the global, by aiming `dsk_dest_x` at that claim and
letting `fmv_store`'s existing copy do the work. One copier, two kinds of
source, and no second opinion about what a fresh cache means.

`KD_INIT` cannot refuse — it *"preserves all"* and the window exists before it
runs — so *mandatory* is a refusal ON THE GLASS and not a window that fails
to open. That is the better answer anyway: the caption, the buttons and the
drive menu all still work, and the window heals the moment memory frees.
