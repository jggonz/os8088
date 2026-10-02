# The disk stops holding the machine

**OPEN, and NOT STARTED.** Nothing here is built. It is the plan for making
floppy and hard-disk I/O stop freezing the operating system, on every machine
class this project targets, and it is written down before a line of it is
written because each of its six waves changes a SPEC section that other code
leans on without saying so.

**The owner's decision that sets the scope** (2026-10-01): *all* of it — the
slicing, the disk mutex, the BIOS hook and the native controller paths —
because the targets are XT **and** AT class machines and the tree uses every
piece of hardware involved. docs/plans/completed/UI-FREEZE-PLAN.md §4.3
recommended against genuine concurrency on a measurement; that recommendation
is overruled by this decision, and §1.4 below is the evidence that was not in
front of it then.

**Read first:** docs/plans/completed/UI-FREEZE-PLAN.md (all of it — this file
is its §4, built out), SPEC.md §7 and §7.4, §8 and §8.5, §12.8.3–§12.8.4,
§18 (opening), §18.9.3, §20.6 rule 7, §52.1, §66.3, §77.41.2.

---

## 1. What holds the machine, exactly

### 1.1 Two locks and a ROM

| | held by | stops | for how long |
|---|---|---|---|
| `[sch_lock]` | `dsk_xfer`, `kernel/disk.inc:1258`–`:1696` | every other task | one transfer |
| `[gfx_lock_flag]` | the window callback that started the work | all painting, and the arrow's right to move | the whole handler |
| the ROM | `int 13h` itself | the calling task | until the controller's IRQ |

The third row is the one the first two hide. SPEC.md §15.3.8 measured the CPU
inside a floppy `int 13h` on the 5150: 83% of a load at CS = F000, **21
consecutive timer interrupts taken with IF set**, the ROM spinning on IRQ6
while DMA does the work. Removing the two locks without getting that wait
back buys other tasks the CPU only in the gaps *between* `int 13h` calls.

### 1.2 What "nothing else runs during `int 13h`" quietly protects

`[sch_lock]` is documented as protecting the BIOS and the FAT globals. The
scouting for this plan found it is also the only thing standing between the
machine and **five** defects that open the moment another task can run inside
a transfer. Every one must be closed in the same wave that lowers the lock:

1. **The mouse ISR draws over another task's primitive.** `mou_apply`'s
   `[cur_inxfer]` arm (`kernel/mouse.inc:3689`–`:3740`, SPEC.md §7.4.2) tests
   "the gfx lock is held by `[sch_cur]`". During a park `sch_cur` is a
   *different* task; if it holds the lock mid-`gfx_fill`, the test passes and
   IRQ4 writes through its Graphics Controller state — SPEC.md §12.8.4 and
   docs/FIELD-NOTES.md §34 again.
2. **The progress widget becomes a second painter.** `fpg_step`/`fpg_arm`
   draw unlocked (`kernel/fprog.inc:8`–`:29`), checking lock ownership once
   at arm time. Worse, *another* task's `gfx_unlock` runs `fpg_finish`
   (`kernel/vga12.inc:4707`) and the busy-pointer restore (`:4727`) and takes
   the transferring task's widget down mid-operation.
3. **DMA writes into memory the heap has given away.** `mem_shed_one`
   (`kernel/memory.inc:1060`–`:1099`) never asks `mem_in_xfer`; it refuses only
   a *dirty* FAT window. A claim on another task can shed the read-ahead cache
   while `dsk_rah_fill` is filling it, or the FAT window while
   `dsk_fat_window` is loading it.
4. **`[mem_pinseg]` is one word restored as a stack** (`kernel/memory.inc:4736`,
   pushed and popped by `dsk_xfer` and `clip_put`). Interleave two tasks and
   A's pop wipes B's pin. `mem_in_xfer` also tests ES only, not ES:BX + length.
5. **Three more single-copy LIFOs**: the driver block staging
   (`drv_blkfp`/`drv_blkseg`/`drv_blkcls`, `kernel/driver.inc:1870`, whose
   comment at `:4720` says it is safe *only* because `dsk_xfer` holds the
   lock), the package nest stack `wm_pkgd`/`wm_pkgs` (`kernel/wm.inc:11517`,
   which `mem_in_nest` trusts to decide whether an image may move), and
   HDD.DRV's request globals (`drivers/hdd/hdd.asm:1313`–`:1318`).

SPEC.md §7's sentence *"exactly two routines raise it"* is also out of date:
today's raisers are `dsk_xfer`, `dsk_dbg_raw`, `dsk_free_clus_x` (twice),
`mem_compact`, `xm_copy`, `hbm_wrimg`, `spk_pcm_run`, `osapi_snd_play` and
`sch_rhook`. Wave 0 corrects the text.

### 1.3 `int 13h` calls that never go through `dsk_xfer`

Any mutex that replaces `sch_lock` must cover these too, or it covers nothing:
`int 13h AH=08h` at `kernel/clone.inc:337` and `:571`, `kernel/diskw.inc:4713`
(the format probe), `kernel/desk.inc:354` (boot); HDD.DRV's `hd_bios_geom`
(`drivers/hdd/hdd.asm:304`) and `hd_raw` (`drivers/hdd/mount.inc:411`, nine
callers — partitioner, formatter, installer, tool), **which already run with
no `sch_lock` at all**, so on the field 5150 the ST11M's ROM is already being
pre-empted mid-transfer on those paths today.

### 1.4 Why this is worth doing now: the waiting consumer

SPEC.md §77.41.2 measured FTPD with a second staging buffer: `wait` fell from
11,554 to 198 ms and the rate did not move — 30,455 B/s both ways — because
*"while the UI task is writing, no task runs at all."* `FD_STG2` is 0 and the
code is kept *"worth exactly what it costs the day the disk stops holding the
scheduler."* docs/plans/completed/FTP-PERF.md ranks it the only candidate over
its 10% bar, at up to 31%. The Audio Player's 16KB gulps (SPEC.md §86), the
Video Player's unbuilt *Live fed from the disk* (§98) and Tracker's
one-read module load (§45) are the same wall.

---

## 2. The ROM survey — which machines tell the OS they are waiting

AT-class BIOSes call `int 15h AX=9001h` (diskette) or `9000h` (fixed disk) just
before they wait for the controller's interrupt, and call `int 15h AX=9101h` /
`9100h` from that interrupt's handler. IBM put the pair there so a multitasking
OS could block the caller and run something else. **Nothing in this tree hooks
`int 15h` today.**

Measured 2026-10-01 by scanning every ROM image the tree's emulators boot for
an `int 15h` preceded within ten bytes by `mov ah,90h/91h` or `mov ax,90xxh/91xxh`.
**Split even/odd ROMs must be interleaved first** — scanned as stored, every
one of them reads as "no", which is how the first pass got the 286 and 386
machines wrong.

| ROM | 90h | 91h | `vm/` machines on it |
|---|---|---|---|
| IBM 5150, all four (24APR81, 19OCT81, 16AUG82, 27OCT82) | — | — | `ibmpc82` (1) |
| IBM 5160 XT, 16AUG82 / 08NOV82 | — | — | `ibmxt` (5) |
| **IBM 5160 XT, 10JAN86 / 09MAY86** | 4 | 2 | **`ibmxt86` (16)** |
| IBM XT 286, 21APR86 | 5 | 3 | — |
| AMI 286 (`AMIC206`) | 7 | 2 | `ami286` (14) |
| MR 286 | 5 | 3 | `mr286` (1) |
| Micronics 386 | 12 | 8 | `micronics386` (11) |
| Shuttle 386SX | 14 | 4 | `shuttle386sx` (1) |
| Packard Bell Legend 300SX | 6 | 4 | `pbl300sx` (1) |
| AMI 486 (`SIS471BE`) | 7 | 2 | `ami471` (1) |
| Award Triton (`p54tp4xe`) | 4 | 4 | `p54tp4xe` (1) |
| **GLaBIOS**, every version 0.2.5–0.4.0 | — | — | MartyPC, and the field 5150's alternate ROM |
| HDD: IBM Xebec 1985 | 1 | 1 | `vm/xt-mfm` |
| HDD: **Seagate ST11M** 1.7 / 2.0 | — | — | **the field 5150**, `make pc5150` |
| HDD: WD1002 / WD1004 / DTC / XT-IDE Universal BIOS | — | — | — |

So the `int 15h` hook is **free coverage for every AT and the 1986 XT**, and
**useless on exactly the machine this project is calibrated against**: the
5150's ROM, the 1982 XT and GLaBIOS issue nothing, and neither does the
ST11M on the field 5150's hard disk. A static scan is evidence, not proof —
§4 W3 makes the kernel *observe* the calls at boot and decide from that.

---

## 3. The coverage matrix — which mechanism gets which machine

| device | ROM says 90h/91h | ROM silent | QEMU / SeaBIOS |
|---|---|---|---|
| floppy | **W3** the hook | **W4** native FDC transport | nothing parks (§3.1) |
| MFM hard disk | **W3** (Xebec, AT BIOS) | **W5** ST11M — §6 decision 1 | — |
| IDE, rung 1 (286+, PIO) | n/a — the driver owns the wait | **W5** yield in BSY/DRQ, then IRQ14 | testable (`make test HDD=`) |
| XT-IDE option ROM | — | CPU-copied: little idle to win; W2's pre-emption is all there is | — |

W1 (slicing) is independent of the matrix and helps everywhere, including
QEMU.

### 3.1 QEMU cannot test a park, and that shapes every gate

`sch_switch` refuses unless SS = `LOW_SEG` (`kernel/sched.inc:2390`–`:2402`,
SPEC.md §8.5). SeaBIOS runs `int 13h` on a stack of its own, so under QEMU a
hook that asks to park is told no and the transfer completes as it does
today. That is the *correct* behaviour and it means:

- every park path must handle the refusal (restore `T_STATE`, carry on);
- QEMU rows can prove "nothing broke" and nothing more;
- **the park is tested on MartyPC and 86Box**, and MartyPC runs GLaBIOS,
  which never signals — so W3's hook is exercised only on 86Box's `ibmxt86`
  and AT machines, which cannot assert (docs/TESTING.md). §6 decision 5.

---

## 4. The waves

Each wave ships on its own, leaves the machine no worse than it found it, and
updates its SPEC sections **before** its code (CLAUDE.md).

### W0 — the defects the scouting found, which exist today

Small, independent, and worth shipping before anything else:

- **`fcp_paste` has no busy test** (`kernel/filecp.inc:376`; only the door at
  `:1937` has one). File > Paste from the menu bar during a *Replace?*
  question overwrites the operation record and leaks the first `MEM_K_COPY`
  claim (up to 64KB). Confirm on glass, then refuse.
- **Nothing refuses a destructive verb against a live copy**: `dsk_vol_del`
  (`kernel/disk.inc:2403`), Format (§22.12 tests only "is a floppy"), Clone,
  Delete/Rename/Rmtree on a folder in the copy's path, and Hibernate (no
  `fcp_busy` reader in `kernel/hiber.inc`). Each gets a refusal that names
  the reason (§47).
- Stale text: SPEC.md §7's raiser list; SPEC.md §8's `MAX_TASKS` = 7 (it is
  14 / 5); `drivers/hdd/hdd.asm:687`'s *"ONE SECTOR PER CALL"* (it batches);
  UI-FREEZE-PLAN §4.1's `dsk_relist` (gone — `fcp_unbatch` →
  `dskw_sync_x` does that job now).

### W1 — slice long operations (UI-FREEZE-PLAN §4.1, built)

What the UI gets: between slices the desktop is fully live — windows paint,
menus open, other tasks run — on every machine and every ROM.

- **The kick is a `ui_task` ladder byte, not `EVT_WAKE`.** A wake needs a live
  window with `W_ONWK`; no kernel window installs one, Disk windows have no
  close hook (`kernel/files.inc:269`), and a wake to a dead or reused slot is
  silently dropped or misdelivered (`kernel/wm.inc:2421`). A copy kicked that
  way stalls for ever holding `fcp_busy`, its buffer and (kern_small)
  FILECP.DRV. `ld_pending`'s `.chk_ld` rung in `kernel/ui.inc:698`–`:760` is
  the precedent: one compare a pass, owned by nobody's window.
- **`FCPS_MORE`**, a fourth answer beside DONE/ASK/ERR. **The first grain is
  one file** — `fcp_step`'s `.walk` — because that is where the state machine
  already resumes. A slice ends at a file boundary once a tick budget is
  spent, so many small files share a slice. **Mid-file resume is a second
  step**, not the first: the read cursor (`fcp_rclus`/`fcp_roff`/`fcp_rsz`)
  and `fcp_made` are already in `.bss` and `dskw_append_x` is stateless by
  name, so it needs one "inside a transfer" flag and a re-entry at `.chunk`
  — but it also exposes a half-written destination between slices, which
  needs a decision (§6 decision 3).
- **The batch must end at every slice end.** Today only `gfx_unlock` zeroes
  `dsk_batch` (`kernel/vga12.inc:4756`); a lock-free slice leaves it raised, the
  next `dsk_batch_begin_x` is a *nested* begin, skips `dsk_bpb_flopzap`, and
  trusts a floppy BPB across a gap in which the user could swap the disk.
- **Operation identity.** A new batch re-reads the BPB but nothing compares it
  with the disk the operation *started* on, and `dsk_bpb_sig` cannot tell two
  os8088-built disks of one geometry apart (`kernel/disk.inc:4519`–`:4527` —
  `tools/os88disk.py` pins the volume serial). Bank a per-volume identity at
  operation start that includes something the user's data changes (a FAT
  sector's hash), compare at each slice's first stand, abort with the reason
  on a mismatch.
- **The progress widget gets an operation owner** (SPEC.md §12.8.3 is
  rewritten, not patched):
  - `[fpg_on]` splits into *chrome is on the glass* and *a painter is
    mid-fill*; only the second gates the mouse ISR, or the arrow freezes
    between slices — the opposite of the point;
  - `gfx_unlock`'s `fpg_finish` skips teardown while an operation owns the
    widget; the owner releases it at `fcp_stop`, with a backstop when
    `fcp_busy` drops so nothing can strand it;
  - the per-hold cursor debt (bit 1) and the busy clock are still repaid at
    every slice end;
  - `fpg_arm`'s refusals (fsx, fullscreen, display change) are re-asked per
    slice.
- **A running operation is not `FS_EDIT = 4`.** Today any click in a Disk
  window during the question *stops* the copy (`kernel/files.inc:7071`), and
  Ctrl+V means stop. A running state of its own, with a visible Stop.
- **The `OSAPI_FILE_COPY` door stays synchronous** — it reads `[fcp_err]`
  straight after `fcp_run2` (`kernel/filecp.inc:1966`). It loops internally.
- kern_small: `fcp_pfin` keeps the module image only on ASK
  (`kernel/filecp.inc:2362`); MORE joins it, and the resume entry is a fifth
  published entry (`FCP_NENT`).
- **Then the other long operations, by how resumable they already are**: the
  HDD installer (its tree walk is already an explicit stack, §52.10.13), Clone's
  pass, Rmtree, Compress. **Hibernate is never sliced** — the image is one
  instant — and refuses while any sliced operation is live. Folder open,
  package launch and the file dialog stay monolithic: they are short enough
  that W2–W4 are their answer.

### W2 — the disk mutex, the wait primitive, and the five invariants

What the machine gets: on ROMs that run `int 13h` on the caller's stack (IBM,
AMI, GLaBIOS — not SeaBIOS), the scheduler may pre-empt *inside* the ROM, so
workers, sound refill and the NIC keep running through a transfer. On a
signalling ROM that is the floor W3 builds on; on a silent ROM the disk task
still burns its slices spinning, which W4 fixes.

- **`dsk_mtx`: owner + depth, re-entrant for its owner.** It must be:
  re-entrant (`dsk_rah_fill` calls `disk_read_x` from inside `dsk_xfer`,
  `kernel/disk.inc:5659`; `hbm_wrimg` writes while holding it); **never waited
  on with `sch_lock` raised** (`task_yield` under the lock returns to the
  caller, `kernel/sched.inc:1192`, so the wait would spin for ever — and
  `hbm_wrimg` must take the mutex *before* `inc [sch_lock]`); in `.text` with
  real initialisers (`-f bin` zeroes nothing, `cur_inxfer`'s argument); a
  plain `inc`/`dec` of `sch_lock` on kern_dos, which has no scheduler
  (`kerndos/kdshim.inc:250`); and reset on hibernate wake beside
  `kernel/hiber.inc:3081`/`:3330`. Waiting parks the way `gfx_lock` does
  (`inst_park_lk`/`unlk`, §66.5.4).
- **Its scope is ONE TRANSFER in this wave**: `dsk_xfer` plus the §1.3 raw
  sites, and nothing above them. The FAT/volume globals stay protected by
  §20.6 rule 7's "only the UI task does file I/O" — still true, so the mutex
  is uncontended and its whole job is to let *other* tasks run. Scoped this
  narrowly it cannot invert with the gfx lock: nothing inside `dsk_xfer`
  draws or takes it (SPEC.md §7.4: *"a block verb that drew would be a
  defect"*). W6 widens it.
- **`sch_lock` stays** around what is genuinely atomic and is not a wait:
  `dsk_free_clus_x`'s two counting holds (pure arithmetic over a sheddable
  ES), `dsk_dbg_raw`, the hibernate write.
- **The wait primitive the scheduler does not have.** `sch_wait(E, timeout)`
  in the shape of `ui_task`'s `.idle` (`pushf`/`cli`, test the byte, sleep,
  loop) and `sch_wake_slot(AL)`, ISR-safe like `sch_wake_ui`, which also sets
  `sch_idlewake` so the idle task's `hlt`/yield makes the wake immediate.
  **State choice**: reuse `T_STATE = 2` with `T_WAKE` as the timeout (no
  snapshot or Task Manager change; the waiter re-tests E because
  `sch_wake_ui`'s unconditional store readies task 0 on every mouse packet),
  or a new state — which must be **4**, because the snapshot ABI already
  reports the idle task as 3 (`kernel/instance.inc:2340`). Reuse 2.
- **The five invariants of §1.2, closed here:**
  1. `[cur_inxfer]` holds the *transferring* task's slot + 1, and `mou_apply`
     compares `gfx_lock_own` against it, not against `sch_cur`;
  2. `fpg_step`/`fpg_stepb` re-test lock ownership per step and skip the draw
     if another task holds it; `fpg_finish` runs only for the widget's owner;
  3. `mem_shed_one` refuses what `mem_in_xfer` reports;
  4. `[mem_pinseg]` becomes a per-task pin (one word per slot, indexed by the
     transferring task) and `mem_in_xfer` tests ES:BX through the length;
  5. the block staging moves onto the caller's stack frame, and the nest
     stack learns that a frame may be pushed by one task and outlive a switch
     (per-task depth, or refuse the compactor while any transfer is in
     flight — the cheaper answer, and the compactor already runs rarely).
- **Accounting**: `sch_hold` and the cooperative watchdog
  (`kernel/sched.inc:1530`) assume a floppy read accrues no hold ticks; once a
  switch can happen inside `int 13h`, that comment and the behaviour change.

### W3 — the `int 15h` 90h/91h hook (AT class and the 1986 XT)

What the machine gets: the transferring task **blocks** for the whole
controller wait and the CPU goes to whoever is ready — the full §15.3.8 idle
time, for floppy and fixed disk alike, on 46 of the 52 `vm/` machines
(every one but the five `ibmxt` and the one `ibmpc82`).

- **Arming.** The hook parks only when *all* of: `dsk_xfer` armed it (a
  task-scoped byte beside `CURXFER_ON`); AL is `00h` or `01h` (and `FDh`, the
  motor-start wait, which becomes a `task_sleep` of the DPT's motor-start
  time — an AT busy-waits about a second there, `kernel/disk.inc:1225`); SS =
  `LOW_SEG`; `[sch_lock]` = 0; the task is fsx-eligible; not kern_dos.
  Anything else chains to the ROM's vector untouched, CF and AH preserved —
  `int 09h`/`int 16h` issue 4Fh and 90h/91h type 02h on an AT and must pass.
- **91h is a store and a chain**: it runs in the ROM's IRQ6/IRQ14 handler, on
  whatever stack is current, after the ROM's EOI. It sets the posted byte and
  calls `sch_wake_slot`. Nothing more.
- **90h returns CF = 0 after the post**, so the ROM's own spin on
  0040:003E bit 7 exits at once; **CF = 1 on our own timeout**, which the ROM
  reports as TIME_OUT through its ordinary retry path. *These semantics are
  from the published BIOS interface and must be verified against one AT
  ROM's code before W3 ships* — the scan in §2 found the calls, not their
  register contract.
- **Detection, not assumption.** The hook counts 90h calls of each type
  during the boot's own reads and publishes the verdict in §57's registry
  (and sysbench), so a field report states which path a machine took. W4 is
  selected *only* where the floppy count is zero.
- **Lifecycle**: installed after `sched_init` (`kernel/kernel.asm:5542`),
  before `drv_boot_x`; the entry in `.text`; unhooked in `sched_unhook` **and
  in `hbm_handover`** (`kernel/hiber.inc:2755`) — the hibernate stub and the
  DOS hand-off issue `int 13h` while the kernel image is being overwritten,
  and an AT ROM's 90h would jump into it. Gated out of kern_dos (`KD_BUILD`).
  The windowed DOS box banks the whole IVT and BDA 0040:0098 already
  (`apps/dos/dos.asm:2085`, `:10877`); a program's own `int 13h` reaches the
  hook unarmed and chains.

### W4 — a native floppy transport (the 5150, the 1982 XT, GLaBIOS)

What the machine gets: the same as W3, on the machines whose ROM never
signals.

- **The seam is the two `int 0x13` instructions in `dsk_xfer`'s BIOS arm**
  (`kernel/disk.inc:1588`, `:1597`): a near call with the `int 13h` register
  ABI (AH 00/02/03, AL run, CH/CL/DH/DL, ES:BX in; CF/AH out). Everything
  above it — CHS, the cylinder run, retries, the §18.95 cache, the canary,
  fprog — is kept as it is.
- **It is NOT a `DRVC_DISK` driver.** One driver per class (it would collide
  with HDD.DRV); a `DVK_DRV` row is treated as *fixed* (§18.7.2), so its BPB
  would be validated once ever, which on a floppy is the removable-media
  defect §18.9.3 exists to prevent; the §18.95 cache is skipped for it; and
  kern_small has no drivers. It is resident: a `.cold` body and a small
  `.text` IRQ6 entry.
- **What it is**: SPECIFY from `dsk_dpt`; motor on via the DOR with the
  spin-up as a `task_sleep`; SEEK and a SENSE INTERRUPT; 8237 channel 2
  (`sbl_arm_half`'s sequence in `drivers/sound/sb.inc:301` is the template, ports
  04h/05h, page 81h, mode 46h/4Ah, under `pushf`/`cli` because the flip-flop
  is shared with the Sound Blaster's channel 1); READ/WRITE DATA; `sch_wait`
  on IRQ6; seven result bytes; the ROM's status code in AH.
- **The IRQ6 handler is a superset of the ROM's**: set 0040:003E bit 7, wake
  the waiter, EOI — so a ROM `int 13h` issued by anything else (the DOS box,
  the park at shutdown, a BIOS fallback) still finds the state it expects.
  The BDA motor bits (0040:003F) and the motor count (0040:0040, 0FFh during
  an operation, the DPT's value after) stay the ROM's, so the ROM's own
  `int 08h` turns the motor off as it always has.
- **Reuse**: the §18.97 probe already speaks to the FDC — `fdd_put`,
  `fdd_get`, `fdd_res`, `fdd_sidrain`, `fdd_dor`, `fdd_st3`
  (`kernel/disk.inc:8617`–`:8790`) — but they live in `.ovlw`, freed after
  boot, and `fdd_wait` spins on `[ticks]`. They move out of the overlay and
  learn to yield.
- **XT-class only.** AT ROMs track the current cylinder per drive (0040:0094)
  and the data rate, and every AT ROM in §2 signals — so the AT stays on W3.
- **Fallback**: any controller answer the transport does not understand
  retries through the ROM's `int 13h`, once, with the scheduler held as
  today. A machine is never left without a working floppy.
- **Cost**: the largest resident addition in the plan. kern_big's image and
  cold rungs have **2 bytes left each** (tools/kernsize.py, 2026-10-01) with
  22,528 bytes of budget spare, so W4 crosses rungs by construction; budget
  it before writing and measure both variants. Whether kern_small gets it is
  §6 decision 4.

### W5 — hard disks

- **IDE rung 1 — yield first, IRQ14 second.** `hd_ide_wait` and `hd_ide_drq`
  (`drivers/hdd/hdd.asm:903`–`:955`) poll up to 65,536 times and never
  yield — a count, not a time, ~0.2 s on a 286 and far shorter than a drive's
  legitimate spin-up. First: a few polls, then `OSAPI_TASK_YIELD`, with a
  deadline in ticks. Then, optionally: nIEN = 0 at 3F6h, hook `int 76h` (the
  `sbl_unhook` order, `drivers/sound/sb.inc:3178`), the ISR reads status, EOIs
  both PICs, and wakes the waiter. This needs W2 — today a yield inside the
  driver returns at once because `dsk_xfer` holds `sch_lock`, and no API slot
  lets a driver drop it (`kernel/xmem.inc:545`).
- **HDD.DRV's globals go under the mutex**, and `hd_raw` and `hd_bios_geom`
  take it — they run unserialised today.
- **MFM through the ST11M ROM** (the field 5150): nothing signals, so the
  choice is §6 decision 1.

### W6 — a worker may do file I/O

What packages get: FTPD's `FD_STG2` turns on, the Audio Player streams, The
Wire writes while it fetches, Tracker preloads the next playlist entry, and
the Video Player's *Live from disk* becomes buildable.

- **The mutex widens to one OPERATION** — from `inst_vol_enter` through the
  `dskw_*` call — because a worker and the UI task now race on the FAT
  globals, `api_name`/`api_name2`, the `dskw_*` scratch, the kept entry
  (§18.4.3), the batch and the per-instance folder. The lock order is **gfx
  then disk, always**: a holder of the disk mutex never takes the gfx lock.
  The loader is the one place that inverts it today (`ld_start` takes
  `ct_cw_gfx_lock` after the read, `kernel/loader.inc:1251`), and it must
  release the disk mutex first.
- **§20.6 rule 7 is rewritten**, and **enforced**: today it ends *"None of
  this is enforced"*. A file slot called from a worker on a kernel without
  W6, or a forbidden slot (the file dialog, which draws) from a worker on one
  with it, refuses with CF = 1 rather than corrupting the FAT.
- **The write API is a caller-held cursor, not APPEND.** docs/plans/DISK-CPU-PLAN.md
  §6: every `OSAPI_FILE_APPEND` walks the chain from the front, so a file
  written in chunks costs time quadratic in its length — VIDDISK's 12.8MB
  took 700 s, ~400 of them walking. Background writes on top of that would
  only hide the cost. `OSAPI_FILE_WRITE_SEQ`, mirroring `READ_SEQ`, with an
  explicit close that commits the directory entry once.
- **A worker blocked on the mutex is a park point**: it parks under
  `OSAPI_MEM_PARKSAFE` like a `gfx_lock` waiter, and its transfer's
  destination is pinned for the whole request by W2's per-task pin.
- First consumer: FTPD, because §77.41.2 is already the A/B — `FD_STG2 = 1`
  and re-take the netbench.

---

## 5. How each wave is proven

| wave | QEMU | MartyPC (GLaBIOS 5150) | 86Box | field |
|---|---|---|---|---|
| W0 | the refusals, on glass | — | — | — |
| W1 | a live UI between slices: a window repaints and a menu opens during a long copy, screenshot per claim; swap the B: image mid-copy and see the abort | the same, timed | — | the 5150 copy, felt |
| W2 | **nothing parks** (§3.1): every existing row stays green — that is the whole claim | a worker's counter advances during a floppy read; the five invariants each get a row that fails with the fix removed | — | — |
| W3 | nothing parks | GLaBIOS never signals: the detector reads **0** | `ibmxt86` and an AT: detector non-zero, look at a live UI during a load | an AT owner's machine |
| W4 | — | the transport is selected; reads and writes byte-identical to the ROM path over every geometry; a worker runs during the IRQ6 wait | `ibmpc82`, `ibmxt` | **the 5150 — the machine it is for** |
| W5 | `make test HDD=` drives rung 1 (the only place it runs at all — `docs/plans/HDD-RESIDENT-PLAN.md`) | — | `xt-mfm`, `286` | the ST-225 |
| W6 | FTPD's STOR rate with `FD_STG2 = 1` | — | `xt-wire` | — |

The A/B for each wave is a knob in `NOCURDISK`'s shape — `NODSKMTX=1`,
`NOI15HOOK=1`, `NOFDCNAT=1` — stamp-tracked, byte-identical to the kernel
before the wave, and the thing that keeps the old path assembling.

---

## 6. Decisions that are the owner's

1. **The field 5150's hard disk.** The ST11M's ROM signals nothing. Either
   **(a)** W2's pre-emption inside its ROM is the answer — other tasks get the
   CPU between the ROM's own polls, the disk task burns its slices, the ROM's
   timeouts must be checked for counting `[0040:006C]` ticks (a descheduled
   task would see one fire falsely) — or **(b)** a native DCB rung at 320h,
   DMA channel 3 and IRQ 5, which **reverses SPEC.md §52.1's refusal** (*"an
   8237 path, an IRQ 5 hook and a per-card jumper matrix nobody can test"*)
   and must also recover the geometry the ST11M keeps on the platter. The
   86Box `st506_xt` model is the only reference to build against; only the
   field 5150 can time it. Recommended: (a) first, measured on the iron, and
   (b) only if (a) is not enough.
2. **W4's bytes.** Resident on kern_big, crossing the image and cold rungs
   (2 bytes left on each), against 22,528 of budget. The alternative — a new
   "BIOS transport" driver class, so it is paid only on a machine that loads
   it — costs a mechanism and leaves kern_small out.
3. **The slice grain.** Per file (cheap, the state machine already resumes
   there; one 63.5KB chunk is still seconds on a floppy) or per chunk (the
   UI is never gone for more than one transfer, and a half-written
   destination is visible between slices — hidden, or shown as a file whose
   size grows?).
4. **kern_small** — the 128KB floor machine, `MAX_TASKS` = 5. W1 helps it as
   much as anything; W2–W4 cost it resident bytes it has (36,352 spare) for
   a machine with few tasks to run in the gap.
5. **Testing W3.** MartyPC's machines run GLaBIOS or a 1981–82 IBM ROM,
   none of which signals, and 86Box cannot assert. **MartyPC already defines
   the 1986 5160 ROMs** (`ibm5160_86_v2`, `ibm5160_86_v3` in its
   `romdef_ibm_pcxt.toml`); only the image files are absent from its
   `media/roms/`, and 86Box holds them under `roms/machines/ibmxt86/`. A
   MartyPC 5160 on the 1986 ROM gives W3 an assertable host at no cost
   beyond the config. Recommended — the alternative is shipping a scheduler
   change on screenshots.

---

## 7. Refusals and negatives already on record — do not re-derive

- **A visible-but-frozen arrow is worse than a hidden one.** SPEC.md §7.1.4.3;
  `CURFIX` stays a knob. Every wave that keeps the arrow lit must keep it
  *moving*.
- **An unlocked painter and the mouse ISR are two painters.** SPEC.md
  §12.8.4. W1's widget split and W2's invariant 2 are arguments *with* that
  section, not around it.
- **A motor-timeout gate for "same floppy" was rejected** (SPEC.md §18.9.3).
  W1's identity check is a comparison of contents, not of time.
- **A disk service task with callers blocking on it** (UI-FREEZE-PLAN §4.2)
  is not the shape here: it pays a task stack for a task that lives in the
  ROM, and it unfreezes nothing while the caller holds the gfx lock. The
  transferring task blocks *itself* (W2's `sch_wait`), on its own stack.
- **Fewer `int 13h` calls was the larger win before** —
  docs/plans/completed/HANDOFF-DISK-IO.md took the install from 356 to 114.
  That remains true of *throughput*. This plan is about *latency* — the UI
  and the other tasks — which call-count work cannot buy.
