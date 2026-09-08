# Splitting HDD.DRV: the transport, and the tool

**Research document, not a contract — and now IMPLEMENTED.**
> The study below was written before the code and is kept as the reasoning
> behind it. What shipped is SPEC.md §52.11; where the two disagree, SPEC.md is
> the contract. The predictions that were wrong, and the three bugs the build
> sprang, are recorded in §10 at the end.

**Research document, not a contract.** SPEC.md is the binding contract for what
the kernel and its drivers *are*; this was the study of a change nobody had
made yet.

The ask, in the requester's words:

> The hard drive driver is two packages squashed into one: one that knows how
> to mount and interface with a hard drive, and one that knows how to draw
> user interface in control panel, and partition/format/install. The second
> does not need to be in ram. Investigate splitting the hard drive driver into
> two components — one loaded when ticking 'Hard Drive' in the Driver page of
> control panel, and the other loaded by clicking a button in the control
> panel's 'Hard Drive' page.

---

## 1. The verdict, up front

**The premise is right, the cut is cleaner than it looks, and it needs no
kernel change at all.**

| | today | predicted | **built** |
|---|---|---|---|
| resident image | 14,576 B | ~6,100 B | **6,657 B** |
| resident heap claim | **15 KB** | 6 KB | **7 KB** |
| tool image (only while loaded) | — | ~9,200 B | **10,241 B → 11 KB** |
| peak, tool loaded | 15 KB | 16 KB | **18 KB** |
| sectors read at boot when ticked | 29 | 12 | **14** |

Two numbers are measured rather than estimated and the method is in §2 and §4:
the 14,576 is the shipped `build/hdd.bin`, and the 5,788 is a *severed*
resident that assembles clean under `-w+error` with the tool half physically
deleted. The severance is the load-bearing result — it is the difference
between "these bytes add up" and "these bytes can actually leave", and nasm
found only **three** dangling references in the whole driver when the tool half
was cut out (§4.2).

**The steady-state win is 9 KB of heap, and where that matters is not where it
is biggest in absolute terms.** `kernsize` puts the heap floor at 91.5 KB, so:

- a **128 KB** machine has 36.5 KB of heap, and the driver goes from **41% of
  it to 16%**;
- the **640 KB** field 5150 (docs/FIELD-MACHINES.md) has 548.5 KB, and the
  driver goes from 2.7% to 1.1%.

So the honest framing is: on the calibration machine this is a tidy-up, and on
a small machine it is the difference between a hard disk being affordable and
not. The requester's own argument — *the second does not need to be in RAM* —
stands on its own without the memory table, and it is the better argument.

**The one field-visible win is the boot.** SPEC.md §51.3 means nothing is
wanted by default, so this applies to a machine that has ticked Hard Drive: the
resident drops from 29 sectors to 12. At PERFORMANCE.md's ~24 ms per 512 bytes
(Part 2, Set 24 — this said 1.1 s from Set 17's 65 ms) that is **~0.4 seconds
off every boot** — inferred from the per-sector
figure, not measured on iron, and it belongs in a field run before it is
quoted as fact. The reciprocal cost is that clicking **Format** or **Install**
then reads ~18 sectors, about 1.2 s, once — paid at the start of an operation
that already takes minutes.

**And the peak gets 1 KB worse, not better.** With the tool open the machine
holds both images. That is the correct trade — a tool window is open for
seconds a session and the resident is up forever — but it should be stated
rather than discovered.

---

## 2. What is actually in the driver today

Measured, not estimated. `hdd.asm` was copied to a scratch file with a label
before and after each `%include` and a `[map all]` directive; labels emit no
bytes, and the instrumented binary is **md5-identical** to the shipped
`build/hdd.bin`, so this is the shipped layout and not a model of it.

| module | bytes | what it is |
|---|---:|---|
| `hdd.asm` (own code+data) | 3,519 | header, attach/detach, the probe, both transport rungs, the service table, the buffers |
| `cfg.inc` | 467 | the `SYSTEM.CFG` blob (SPEC.md §52.6) |
| `part.inc` | 1,873 | the partition table — **of which 958 is the two `incbin`s** |
| `fmt.inc` | 843 | the FAT formatter (SPEC.md §52.3) |
| `tool.inc` | 1,723 | the partition/format window (SPEC.md §52.2) |
| `inst.inc` | 3,859 | the installer (SPEC.md §52.10.4) |
| `page.inc` | 2,260 | the Control Panel page (SPEC.md §52.4) |
| **total** | **14,576** | |

Three things stand out and each shapes the cut:

- **`inst.inc` alone is 26% of the driver** and can only ever run when a human
  is standing at the machine clicking Install.
- **958 bytes are the two `incbin`s** — `boot/mbr.asm`'s 446-byte chain-loader
  (SPEC.md §52.10.1) and `boot/boothd.asm`'s 512-byte volume boot record
  (SPEC.md §52.10.2). Both are pure *write-path* assets.
- **1,024 bytes are two 512-byte sector buffers.** `hd_sec0`, the formatter's
  sector under construction, is referenced **53 times by the tool half and
  zero times by the resident half**. It is the single largest movable object
  in the driver.

---

## 3. Where the cut falls

A cross-reference of every symbol against every module says the driver is
already two programs with a thin seam, and the seam runs almost entirely one
way.

### 3.1 Resident → tool: four references, three of them call sites

| symbol | from | what it is |
|---|---|---|
| `hd_tw_open` | `page.inc` | the **Format** button |
| `hd_iw_open` | `page.inc` | the **Install** button |
| `hd_iw_shut` | `page.inc` | `hd_win_close_all`, at detach |
| `hd_s_two` | `page.inc` | a string, 2 bytes — move it and the reference dies |

That is the whole of the backward direction. Three entry points is a small
enough ABI that it does not need a table: `BP` = one of three, through the
package dispatcher the kernel already uses (§5.1).

### 3.2 Tool → resident: 67 distinct symbols, and most are not a problem

Sorted by what they actually cost:

- **20 symbols are tool-only and simply move**, having *zero* references from
  the resident half. They are there because everything lives in one file
  today, not because anything shares them: `hd_sec0` (512 B), the tool
  window's state (`hd_tmsg`, `hd_tsel`, `hd_tdev`, `hd_tarm`, `hd_tstate`,
  `hd_tdrop`, `hd_idev`), the formatter's plan (`hd_fspc`, `hd_ffatsz`,
  `hd_ftype`, `hd_fclus`, `hd_fdata`), `hd_ask` (40 B), and `part.inc`'s write
  half.
- **~25 are `equ` constants** — `HP_*`, `HDD_*`, `HDV_*`, `HD_MAXVOL`,
  `HMB_*`, `HPT_*`. A constant costs nothing to duplicate; they go in a shared
  `hddabi.inc` that both images `%include`, and the ABI is unaffected.
- **~8 are small pure helpers** — `hd_utoa`, `hd_utoa4`, `hd_scat`,
  `hd_part_ent`, `hd_part_chs`, `hd_win_erase`. **Duplicate these into the
  tool rather than far-calling them.** A far call per digit is absurd, the
  resident keeps its own copy either way, and duplication costs tool-image
  bytes — which is exactly the currency this change is trying to spend.
- **That leaves ~14 that are genuinely shared state or genuinely shared
  service**, and those are the real ABI (§5.2).

---

## 4. The severance, proved

### 4.1 Method

A scratch copy of `drivers/hdd/` with `fmt.inc`, `tool.inc` and `inst.inc`
deleted from the include list, `part.inc`'s eleven write-path blocks removed,
and the tool-only data removed from `hdd.asm`'s data block — then assembled
with the real flags (`-f bin -w+error`) and the real include paths. nasm is
the oracle: anything the resident still needs shows up as an undefined symbol.

### 4.2 Result: three dangling references, all resolvable

1. **`hd_xend`/`hd_xslot`/`hd_fbase`/`hd_fsecs`/`hd_x*`** — `part.inc`'s
   `hd_slot_extent` and its allocator scan (SPEC.md §52.2.1) still referenced
   the removed data. They *are* tool code; removing the blocks as well fixed
   it. Not a coupling, a mis-filing.
2. **`hd_s_mb`** — a 4-byte `' MB'` the page prints too. Stays resident.
3. **`hd_bootstub` in `hd_mbr_blank`** — the only genuine one, and the only
   behaviour change the split requires. See §4.3.

With those resolved the resident assembles at **5,788 bytes**.

### 4.3 The one real change: `hd_mbr_blank` stops stamping the boot code

`hd_mbr_blank` fabricates an in-memory partition table when the MBR read fails
or the disk is unpartitioned, and it copies the 446-byte chain-loader into
bytes 0..445 while it does so. That is why a *read* path holds a *write* path's
asset.

Nothing resident ever looks at those 446 bytes — the mount walks the four
entries and the signature and nothing else. So `hd_mbr_blank` leaves the code
area **zero**, and the tool stamps the loader in immediately before
`hd_part_write`.

**It must stamp conditionally, on `[hd_mbrok] == HMB_BAD`.** When the table
came off the disk (`HMB_OK`) those bytes are *the disk's own boot loader* and
`hd_part_write` preserves them today; an unconditional stamp would quietly
overwrite a foreign OS's MBR on every partition edit. The installer
(SPEC.md §52.10.1) stamps deliberately and unconditionally, which is a
different operation and stays as it is.

---

## 5. The mechanism

Three ways to load code on demand. The recommendation is the third.

### 5.0 Two that were considered and rejected

**A second `drv_tab` row.** Rejected on the ask: the point is that the tool is
*not* ticked in the Drivers page. It would also need a "hidden row" concept and
a way for one driver to load another, both kernel code.

**An ordinary application package** (`HDTOOL.O88` in `SYSTEM/`, launched the
way the chip menu launches the Task Manager). Genuinely attractive — it would
get an instance record, a dock tile, a Task Manager row, and `mem_free_rec`
would clean up its claims and windows on the standard teardown path, which is
strictly better than anything a driver can arrange for itself. Rejected on
cost: the kernel has no "launch a package by name" slot (`ui_tm_open` is
kernel-internal and hard-coded to `TASKMGR.O88`), and the app would need the
driver's segment to reach the transport, which means a new published slot and
a new fence. **`kernsize` reports 2,048 bytes spare against `KERN_BUDGET`** —
exactly the four-step standard, so the next feature has to ask — and spending
some of it on plumbing for a change whose entire purpose is to give memory back
is the wrong trade. Worth revisiting if a *second* driver ever wants the same
thing.

### 5.1 Recommended: the resident loads the tool itself, as an overlay

`HDDTOOL.DRV` is a second image the resident reads into a heap claim of its own
and far-calls. **This needs no kernel change whatsoever** — every mechanism it
uses is already published and already used by this driver.

Six facts make it work, each verified against the kernel source rather than
assumed:

- **`wm_create` is an X stub**, so `W_SEG` is the *caller's* segment and
  `wm_pkgcall` does `mov ds, [W_SEG]` / `call far [W_DISP]` into the fixed
  `PKG_DISP` offset. A window created by the tool dispatches back into the
  tool exactly as a package's does. The tool image needs the standard 3-byte
  dispatcher at +12 and nothing else.
- **The driver's windows are already unowned** (SPEC.md §38.1's species —
  a driver has no instance), so moving them to a second segment changes
  nothing about how the window manager treats them.
- **The tool's claim is owned by the resident's segment**, because
  `OSAPI_MEM_CLAIM` takes the owner from `ES` and the resident is the caller.
  `drv_release` sweeps `mem_free_owner` over the driver's segment before it
  frees the image (SPEC.md §50.4), so a tool image left loaded at detach is
  freed by the kernel with no bookkeeping of ours.
- **The `.DRV` extension is free attribute handling.** `tools/os88disk.py`'s
  `sys_attr` gives read-only + hidden + system to anything ending `DRV`
  (SPEC.md §19.6), so the tool is hidden from the Disk window and from DOS
  `DIR` with no change to the tool-chain — and the installer's "every `*.DRV`"
  copy (SPEC.md §52.10.4) picks it up for free. Keeping header format **4**
  keeps the application loader refusing it (SPEC.md §51.1), which is what
  stops it being double-clickable. It is not in `drv_tab`, so nothing lists it
  as a driver.
- **Finding the file needs no new API, and this is the neat part.**
  `HDD.DRV` lives on the *system* volume, which is A: on a floppy machine and
  the boot partition on an installed one (SPEC.md §52.10.3) — and a driver has
  no way to ask which. It does not need one: `drv_load` brackets everything in
  `drv_vol_bank` … `drv_vol_back` and calls `drv_mounted` before it reads, and
  `DRVV_READY` is sent from *inside* that bracket (SPEC.md §51.2.2). **At the
  top of `hd_ready` the current volume already is the system volume**, so one
  `OSAPI_FILE_HERE` there banks (drive, cwd) as "where my files are", and the
  tool loader does `OSAPI_FILE_GOTO` to it and back. This is the same
  bank-and-restore `cfg.inc` and `inst.inc` already use (SPEC.md §51.5.2).
- **File I/O from a Control Panel page click is established** — `drv_load`
  itself is called from one.

### 5.2 The ABI

**Resident → tool.** One far call through the tool's dispatcher, `BP` = the
entry: `TW_OPEN` (Format), `IW_OPEN` (Install), `T_SHUT` (close every window,
free every claim, and answer "nothing of mine is on screen"). Plus one init
call handing over the resident's segment and the address of its shared block.

**Tool → resident.** One far call, `AL` = a verb, shaped like `drv_svc_call`.
About twelve: raw device read/write, `hd_dev_row`, `hd_part_load`,
`hd_mount_one`, `hd_unmount_row`, `hd_cfg_mark`, `hd_page_msg`, claim/free
(see the trap in §6.4), and a repaint poke.

**Shared state.** Not 47 scattered symbols reached by `ES:` override — one
explicitly laid-out block in the resident whose offsets live in `hddabi.inc`,
handed over as a pointer at init. It is the `dsk_get_dir` staging idiom and the
`DSV_*` table idiom, and it holds what genuinely must be shared: `hd_mbr` (512
B) and `hd_mbrok`, `hd_devs` and `hd_ndev`, `hd_sel`, `hd_vols`, and the page's
caption.

---

## 6. Five traps

### 6.1 A window that outlives its image is a paint into freed memory
 This
is already the hazard `hd_win_close_all` exists for, and the split gives it a
*second* image with a *second* lifetime. `hd_detach` must call the tool's
`T_SHUT` before it returns, because `drv_release` frees the claim the instant
it does. It is the same discipline as the SDK's worker-task rule, and it fails
the same way: not an error, a hang.

Worth noting in passing that `hd_win_close_all` currently **hides** rather than
destroys, which leaves a window record whose `W_SEG` names freed memory. That
is safe only because nothing re-shows it, and it leaks a `MAX_WIN` slot. It is
a pre-existing wrinkle, not one the split creates — but the split is the moment
to make it a destroy.

### 6.2 A string the resident renders must live in the resident's segment

`hd_page_paint` draws the caption with `font_str`, which reads through DS. The
tool sets that caption in exactly one place today (`tool.inc` → `hd_s_pick`)
and happens to use a resident string, so it survives by luck. After the split
the caption must be a resident offset or staged text, never a tool pointer.
This is the trap SPEC.md §31.9 already documents for `DSV_CPNAME` and the one
SPEC.md §52 records the driver hitting before.

### 6.3 The tool's own heap claims are not swept by the resident's teardown

`inst.inc` claims a copy buffer (SPEC.md §52.10.7). Once that code runs in the
tool's segment, `OSAPI_MEM_CLAIM` stamps the *tool's* segment as owner, and
`mem_free_owner` over the *resident's* segment will not find it. Two ways out;
the second is better: free it in `T_SHUT`, or have the tool ask the resident to
claim on its behalf so every claim in the feature has one owner.

### 6.4 Format now needs the system disk in the drive
 Install already did —
it copies from it — so this costs Install nothing. Format is new: on a
single-floppy machine (the calibration target) the user may have swapped to the
apps disk. The failure is clean and reportable (`drv_mounted` fails, the page
says so on SPEC.md §47's terms) but it is a real regression in convenience and
the owner should decide whether it is acceptable before any code is written.

### 6.5 The peak is 1 KB worse
 §1's table. Not a bug, a trade.

---

## 7. What this does not change

- **No kernel byte moves.** No new slot, no `KERN_BUDGET` conversation.
- **No published ABI changes.** The overlay's interface is private between two
  files in `drivers/hdd/`; SPEC.md §20.8 rule 4 is not engaged.
- **No `.o88` is invalidated.** No application is touched.
- **The transport, the volume table and `DSV_BLK` are untouched** — a mounted
  volume's path to its sectors is the same code it is today.

---

## 8. Decisions for the owner

1. **Is §6.4 acceptable?** Format requiring the system disk is the only
   user-visible regression in the whole change.
2. **Is 9 KB of steady-state heap worth ~1,300 lines of assembly moving across
   a segment boundary?** On a 128 KB machine, clearly. On the 640 KB 5150 the
   better argument is the ~1.1 s boot and the structure, not the memory.
3. **Overlay (§5.1) or application package (§5.0)?** The overlay is
   recommended because it costs no kernel bytes; the package has the better
   teardown story and is the answer if a second driver ever wants the same
   thing.
4. **`HDDTOOL.DRV`, or a name that does not say "driver"?** `.DRV` buys the
   attributes, the installer's copy rule and the loader's refusal for free, at
   the cost of a file that is not a driver being called one.

---

## 9. If it goes ahead

Five commits, each one buildable and testable on its own. The last is the only
one that can break anything.

1. **Move the mis-filed data.** `hd_sec0`, the `hd_t*`, `hd_f*`, `hd_x*`,
   `hd_ask`, `hd_idev` out of `hdd.asm`'s data block and into the modules that
   own them; `part.inc`'s write half into a new `partw.inc`. **One image
   still, and the binary should stay very close to byte-identical.** This is
   the commit that makes the seam visible in the source.
2. **`hd_mbr_blank` stops stamping** (§4.3), with the conditional stamp added
   at the write site. One behaviour change, testable on its own against a
   disk with a foreign MBR.
3. **`hddabi.inc`**: the shared constants and the shared-state block layout,
   with the resident's data re-expressed through it. Still one image.
4. **Build the second image** and wire the loader, the dispatcher and the
   verbs. Two images, both on the disk.
5. **Delete the tool half from the resident.**

**How it gets tested.** SPEC.md §52 is one of QEMU's short list — `make test
HDD=40` — because the field machine's C: is a real DOS 3.3 install that
nothing may write to (docs/FIELD-MACHINES.md). MartyPC's `os8088_xt_hdd` covers
rung 0, which is the rung the field machine uses. The specific things to drive:
Format with the tool loaded twice in one session (the claim must not leak),
detach with the tool window open (§6.1), Install end to end, and a Format
attempted with the wrong disk in A: (§6.4). Steps 1–3 should each be checked
for a byte-identical or near-identical `hdd.bin`, which is the cheap way to
know a pure move was pure.

---

## 10. What happened when it was built

Shipped as SPEC.md §52.11. The shape of the design survived intact — no kernel
byte changed, the seam is where §3 said it was, and `hd_raw`-as-a-thunk did
carry every routine above it across unmodified. Four things were wrong.

**The estimates were light by about 10%.** The severed floor of 5,788 bytes
(§4) was real, but the ABI put back 869 of them: the loader, the volume dance,
the verb dispatcher and the thunks. The tool came out at 10,241 rather than
~9,200 because the duplicated helpers in `hdcom.inc` cost more than the "~8
small pure helpers" of §3.2 — the partition-table read half and a second
512-byte `hd_mbr` are in there too. So the steady-state win is **8 KB, not 9**,
and the peak is **3 KB worse, not 1**.

**The reap had to go.** §5.2 assumed the tool's image could be freed when its
last window closed. It cannot: closing an unowned window is `wm_hide`, the
record survives naming the freed claim, and the next load creates another —
a window-table leak per open/close cycle, which is worse than holding 11 KB.
Freeing at detach keeps it to one record per attach, exactly what the
single-image driver cost. The fix is a published `wm_destroy`, deliberately
not taken (SPEC.md §52.11.3).

**Three bugs, and all three were silent.** None faulted, none hung, and none
put anything on screen — which is the thing to take from this section, because
the cost was almost entirely in finding them rather than in fixing them:

1. **`hd_tool_need` restored BP after the kernel calls that spend it**, so the
   verb the caller had put there was replaced by whatever `OSAPI_FILE_READ`
   left. The tool was re-sent `HDT_INIT` instead of `HDT_FORMAT`, which
   answers CF = 0 having done nothing.
2. **The verb was passed in BP at all.** The dispatcher is `call bp / retf`, so
   BP is an ADDRESS — `call bp` with BP = 1 ran offset 1 of the image, fell
   through the header into its own `0CBh` byte and returned. `drv_call` had the
   answer all along: BP = the entry offset from the header at +6, verb in AL.
3. **`HDT_INIT` was handed `[hd_tseg]` where it wanted `cs`**, so the tool
   built a far pointer to *itself*: every service call landed on its own
   dispatcher with `hd_svc`'s offset in BP, ran whatever was at that offset in
   its own image, and returned. The device table never synced.

**What found them was a memory dump, not reasoning.** Six rounds of reading
the source produced three plausible wrong answers. `pmemsave` over QMP plus a
`[map all]` symbol map of each image — byte-identical to the shipped binaries,
so the offsets are the real ones — showed `hd_rseg = 0x18e0` against a tool
segment of `0x18e0`, and bug 3 was over in a minute. The recipe is worth
keeping: dump 640 KB, find each image by its `'O8'` magic and its name string,
and read its state through the map.

**A fourth was caught by testing rather than by building.** `hd_tool_drop` was
missed when the verb moved from BP to AL, so `HDT_SHUT` never ran and the
installer's window survived a detach — visibly, on screen, over a driver that
had gone. That is §6.1 exactly, and the only reason it was not shipped is that
detach-with-a-window-open was on the test list.

**Verified under QEMU** (`make test HDD=40`), driven end to end and checked
from the HOST, not from a screenshot: the tool loads off the system volume and
its window opens; Format writes an MBR and VBR **byte-identical to the
pre-split driver's** (chain-loader = `build/mbr.bin`, slot 1 type 04 at LBA 63
for 65,535 sectors, VBR `OS8088` / 512 bps / 4 spc / 63 spt / 16 heads); Close
and Mount put HDD C on the desktop; the installer window opens; the tool
reopens from the already-loaded image; unticking the driver with a tool window
up takes the window down and leaves the machine alive; re-ticking reloads and
works again; and Format with the wrong disk in A: answers **`Need the system
disk`** and claims nothing.
