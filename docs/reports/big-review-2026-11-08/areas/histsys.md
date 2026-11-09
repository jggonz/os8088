# LENS: systemic comparison - events, drawing, windows, scheduling, drivers, files, errors

Reviewer key: `histsys`. Tree: `the-big-review` (cut from `origin/elendilon`),
2026-11-08. Read-only: nothing assembled, booted or soaked. Every claim below
cites the line it was read off. "PLAUSIBLE" means traced in the code and not
reproduced on a machine.

## Scope read

| file | lines | how |
|---|---:|---|
| `kernel/events.inc` | 354 | all of it |
| `kernel/sched.inc` | 3,092 | 1-1150, 1392-1445, 1796-2800 (everything but the KFZ/DRVDIAG instrument bodies) |
| `kernel/ui.inc` | 3,408 | 1-1400 (the whole of `ui_task`, `ui_bill`, the ladder), banners to 1460 |
| `kernel/wm.inc` | 12,041 | record layout 1-200, `wm_strseg`/`wm_pkgcall` 340-520, `wm_wake*` 2280-2470, `wm_destroy` 3040-3180, `wm_paint_dmg`/`wm_dmg_wins`/`wm_paint_all` 5181-5700, `wm_clip_set`/`wm_covered` 5990-6180, `wm_draw_win` 10780-11000, `wm_isrec` 11660-11690. The other ~9,000 lines (zoom, snap, raise cache, title band) were sampled, not read |
| `kernel/vga12.inc` | 7,202 | lock state 280-330, `gfx_lock`/`gfx_unlock` 4980-5270 (entry points only, as briefed) |
| `kernel/driver.inc` | 6,911 | header 1-280, `drv_tab` and the class tables 1236-1520, 1900-2700 (`drv_cls_*`, `drv_call`, `drv_stamped`, `drv_svc_call_x`, `drv_blk_call_x`, `drv_fs_hi` head) |
| `kernel/disk.inc` | 11,457 | `dsk_xfer`'s retry ladder 1240-1300 and 1440-1500 and 1960-2120, `dsk_bootltr` 2880-2930, `dsk_free_clus_x` 6290-6470 |
| `kernel/diskw.inc` | (API layer) | FERR table 56-80, `dskw_read_x`/`dskw_wrap7`/`dskw_dfree_x` 340-520, `dskw_setfat` 4815-4880 |
| `kernel/toast.inc` | 748 | 1-470 (`toast_show`, `toast_say`, `toast_now`, `toast_arm`) |
| `kernel/instance.inc` | (cross-check) | `app_close_win` 1630-1760, `inst_task_die`/`inst_unwin` 1881-1950, `inst_wchk` 1975-2000, `inst_pkg_alive` 2132-2215 |
| SPEC.md | | §7 intro + §7.3, §8 intro, §10 entire, §13 intro, §18 intro + 18.4.5 + 18.9.1, §51 intro, §66.3.1, §75 headings + 75.3 |
| docs | | ABI-BREAK-LIST.md (all rows), KERNEL-BYTE-QUEUE.md, CODE-REVIEW-TWO-CYCLES-2026-11-01.md, STACK-SLOTS-PLAN.md 12.4-12.6, DISK-CPU-PLAN.md 7.2 |

## Verdict

At the level of the MODEL, os8088 is closest to Mac System 6 with MultiFinder's
single event loop grafted onto an AmigaOS-style pre-emptive task table: one
UI task owns all input and all callback dispatch, workers are pre-empted
round-robin, a single global mutex (`gfx_lock`) stands where Windows had a DC
cache and QuickDraw had per-port state, and the file system is DOS's FAT
reached by NAME with no handle table. That combination is sound for an 8088
and in several places better than every peer: the lost-wakeup-proof blocking
UI task (97% halted when idle), the drop-oldest event ring with its explicit
"a wake is a promise, an input is a sample" rule, the posted-work ladder behind
one self-healing byte, the deferred close, and the motor-timeout media check
are each a cleaner answer than DOS, Windows 3.x or the Mac gave. The systemic
weak spots are where a mechanism runs on the WRONG CONTEXT: a dying worker
tears down and repaints on its own 128/192-byte slice (finding 1), every
repaint runs other packages' paint procs synchronously inside whoever caused
the damage (finding 10), every `int 13h` freezes every task for ~400 ms
(finding 5), and the UI task has no scheduling preference at all (finding 4).
The kernel-driver ABI is the area most improved by a flag day: a register-only
verb ABI has produced three dispatch bodies and per-class 36-byte service
copies that a DOS-style request block or a Mac-style parameter block would
collapse (findings 7, 8). The error model has exactly one fatal panel (stack
overflow) and no divide-fault handler (finding 2). Recommendations are all
small: the largest growth proposed is ~+40 bytes, and four of them save
150-300 resident bytes behind an ABI break.

## Strengths

1. **The UI task blocks, and the lost wakeup is closed by construction**
   (`kernel/ui.inc:1156-1184`, `kernel/sched.inc:700-723`, `:750-800`). The
   test of `[sch_uiwake]` and `task_sleep`'s state store are one IF=0 window;
   the idle task's halt is guarded the same way with `sti`'s shadow covering
   `hlt`. Windows 3.x spun in `GetMessage`/`PeekMessage` and relied on WIN386
   (int 2Fh 1680h) to idle; the Mac's `SystemTask`/`WaitNextEvent` slept on a
   timeout. On an 8088 this is the best idle story of any of them. Keep the
   one-word `0x0101` store for both wake bytes.

2. **The event ring knows which records are SAMPLES and which are PROMISES**
   (`kernel/events.inc:95-116`, `kernel/wm.inc:2343-2370`, `kernel/events.inc:223-243`).
   Drop-oldest for input, never-drop for a coalesced `EVT_WAKE`, a three-state
   flag (`WKQ_NONE/RING/EATEN`) so a drain that eats a wake owes it back, and a
   bounded drain per pass. This is the same distinction Windows makes between
   queued input and *synthesised* `WM_PAINT`/`WM_TIMER` (which can never be
   lost because they are flags, not records) and the Mac makes with update
   events - but stated as a rule and enforced in three places rather than
   implied. Copy it anywhere a ring carries a mix (sound grant queues, DOS's
   key ring).

3. **Posted work behind one byte that over-sets for free** (`kernel/ui.inc:730-752`,
   `:1060-1066`). Twenty-odd deferred requests cost one compare on a quiet
   pass, and a setter that forgets to say so is 55 ms late instead of lost,
   because the tick sweeps. This is the right failure direction and it is
   what Windows' "posted messages are processed when the queue is empty"
   never had: a lost `PostMessage` there was lost.

4. **Callbacks enter through the package's own 3-byte dispatcher**
   (`kernel/wm.inc:396-505`; `PKG_DISP`). A package author never writes
   `retf`, the dispatch is re-entrant because the far pointer lives in the
   record, and the same path serves drivers (`drv_stamped`,
   `kernel/driver.inc:2495-2517`). Win16 needed `MakeProcInstance` thunks and
   `EXPORTS` for exactly this problem, and got it wrong for years.

5. **Deferred close: "closed HERE, with none of its code on the stack"**
   (`kernel/ui.inc:869-897`, `wm_clreq`, SPEC.md 75.2). This is
   `PostMessage(WM_CLOSE)` done right - one bit per slot, spent under the
   lock, and `wm_destroy` cancels a request naming the slot it frees so a new
   tenant cannot be closed in its place (`kernel/wm.inc:3112-3117`). It is the
   pattern finding 1 wants extended.

6. **Damage is a rect, visibility is a rect list, and covered windows are
   skipped** (`kernel/wm.inc:5181-5340`, `:6044-6175`). `wm_clip_set` is
   QuickDraw's visRgn without a region allocator: content rect minus every
   visible window above, at most `WM_CLIP_MAX` fragments, and a degrade
   direction chosen per caller (skip the frame for a painter, draw anyway for
   `wm_covered`). Mac regions were general and paid for it in heap churn;
   this is the 8088-shaped subset.

7. **`gfx_lock`'s fairness debt** (`kernel/vga12.inc:5082-5136`, SPEC.md 7.3).
   One byte and one compare turned 1.4-14.7 s menu latency under a drawing
   worker into 37-70 ms. A real ticket lock would have cost a wait word per
   task; this is the cheapest fix that works for a burst-drawing system.

8. **`drv_call` refuses rather than far-calls into zeros**
   (`kernel/driver.inc:2399-2493`): BX is range-fenced before DS is trusted,
   and `DRVR_SEG = 0` is a refusal. DOS's device chain and the Mac unit table
   both happily jumped through a stale entry.

9. **The media-change predicate is a physical fact** (SPEC.md 18.9.1, the
   BIOS motor-off countdown at `0040:0040`). DOS 2-6 used a "two seconds since
   the last access" guess for drives with no change line; os8088 reads the
   spindle. Better than DOS, and free.

10. **Name-based, stateless file calls leak nothing at app death**
    (`kernel/diskw.inc:394-423`; READ_SEQ's caller-owned cursor, "a stale
    cursor is never an error"). DOS leaked SFT entries when a program died
    without closing; Win3.x tied file handles to the task's PDB. Here there is
    nothing to reclaim, which is why the teardown paths in `instance.inc` have
    no file arm at all.

11. **Stacks are checked, measured and diagnosed**
    (`kernel/sched.inc:984-1006`, `:2447-2449`, `:2588-2720`). Unconditional
    0xCC fill, a canary on every slice including task 0's, and a panel that
    moves SP first and names the task. No peer of the era did any of the three.

12. **The toast is the one notification surface, and it treats its input as
    hostile** (`kernel/toast.inc:197-311`): bounded scan, printable ASCII only,
    TTL clamped to the signed range the expiry compare can read. System 7's
    Notification Manager is the closest analogue and it did none of that.

## Findings

Ranked by value. Phase: **now** = no ABI break; **flagday** = needs one;
**record** = write it down, do not recommend.

### histsys-1 (hazard, HIGH, PLAUSIBLE, now) - a dying worker tears down and repaints on its own stack slice

`inst_pkg_alive .die` (`kernel/instance.inc:2196-2210`) jumps to
`inst_task_die` (`:1921-1947`) **on the worker's task**. That takes
`gfx_lock`, then `inst_unwin` -> `wm_destroy` (`kernel/wm.inc:3071-3170`)
-> `wm_paint_dmg` (`:5181`) -> `wm_dmg_wins` -> `wm_draw_win` ->
`wm_pkgcall` -> *another package's* `W_PAINT`, plus `inst_sweep_seg` ->
`wm_destroy_seg` for every other window the package made (destroyed while
still VISIBLE, so a full damage pass), plus `desk_drop_x` on kern_big. All of
it lands on the worker's slice: 128 bytes for Bounce
(`apps/bounce/bounce.asm:32`, `OS88_STACK_128`), 192 for Artful, Fractal,
Notepad, Arkanoid, isoTron, WIRE, The Wire and WEAVE.

STACK-SLOTS-PLAN 12.4 already established the rule ("a slice is sized by what
lands on it") and sized CWORD and RUNCPM at 256 for exactly this close path -
but every other worker-owning package dies the same way and is sized by its
own static depth (`tests/unit/t_stkclass.py` reads the worker's tool depth,
not the teardown's). The common case is softened: the close box hides the
window on the UI task first (`app_close_win`, `instance.inc:1671` -> `.hide` at `:1730`),
so the worker's `wm_destroy` sees a hidden window and takes the empty-rect
`.chrome` arm (`wm.inc:5304-5309`: `wm_dock_under` + `menu_draw_bar`) - which
still composes and letters the menu bar through `font_run` on 128 bytes minus
a 64-byte interrupt floor. A package with a second visible window, or a dock
strip under the closing one, takes the full paint chain. The failure is
`sch_stkdie` on the `task_exit` -> `sch_switch` canary check
(`sched.inc:2447-2449`): a STACK OVERFLOW panel and a dead machine at close.

**Recommendation.** Do the teardown where Windows and the Mac always did it -
on the UI task. `inst_task_die` already has the deferred-close machinery next
to it: set `I_TASK = 0xFF`, post the window's `wm_clreq` bit (or a
"reap instance" byte for a windowless record), and `task_exit` with BX = 0.
The UI pass's `.cl_slot` (`ui.inc:869-897`) then reaches `app_close_win`'s
task-less arm, which is the full synchronous teardown, on the 1,024-byte
stack. The region may not be freed before the worker has stopped executing
in it, and after `task_exit` it has. Gate it with a row that closes Bounce
over a second window under `tools/stkwater.py` - the measurement does not
exist today.

Bytes: **+15 to +30 `.text`/`.cold`** (the post and a windowless arm), and
it deletes the reason CWORD/RUNCPM must declare 256.

### histsys-2 (hazard, MEDIUM, CONFIRMED by reading, now) - no divide-fault handler on kern_big and kern_small

Nothing installs int 0 outside kern_mega: SPEC.md:195770 says so ("INT 0,
which the kernel does not [handle]"), and `kernel/ctrl.inc:5975` records a
shipped bug that reached it. On the 8088 the BIOS's dummy `iret` resumes
*after* the `div` with garbage in AX (the 8086 pushes the next IP). On a
286 or 386 running kern_big - every AT in `vm/`, and the owner's mr286 - the
pushed IP is the faulting `div` itself, so the machine re-executes it for
ever: a hard UI hang with IRQs still running, which reads in the field as
"froze". `wm_ptr2idx`'s `div cl` (`instance.inc:1975-1978` explains it) and
every package `div` on hostile data reach it.

Prior art: the Mac's System Error handler (`SysError`, the bomb box with an
ID), Windows 3.0's UAE box, OS/2's trap screen. All three have ONE fatal path
that names what died.

**Recommendation.** Generalise `sch_diepanel` (`sched.inc:2686-2720`) into a
system-error panel that takes a message pointer, and point int 0 at a 10-byte
stub that loads `sch_s_div` and falls in. Install the vector from the boot
overlay (zero resident for the install). A later step could kill the faulting
instance when CS is a package segment and the task is a worker (kern_mega's
`mega_fkill` is the model), but the panel alone turns a silent hang into a
diagnosis.

Bytes: **~+30 resident** (stub + 'DIVIDE ERROR' string; the panel body is
shared), 0 in the boot overlay.

### histsys-3 (perf, MEDIUM, CONFIRMED by reading, now) - the free-cluster count is recomputed from scratch on every ask

`dskw_dfree_x` (`kernel/diskw.inc:485-509`), `OSAPI_VOL_STAT`, the Disk
window's status figure and every mount call `dsk_free_clus_x`
(`kernel/disk.inc:6337-6470`), which walks the whole FAT. On a FAT16 hard-disk
partition the FAT is ~64 sectors against a 9-sector window, so the walk
faults FAT sectors back in through `dsk_fat_ofs_x` - the routine's own
comment at `.f16` says that call "does disk I/O" - on top of the ~105 ms of
CPU SPEC.md 18.4.5 measured. FTPD spent 44% of an upload here before it was
told not to ask.

Prior art: DOS keeps a free-cluster count in the DPB (`dpb_free_cnt`, 0xFFFF
= unknown, maintained by the allocator); FAT32 persists it in FSInfo.

**Recommendation.** One word `[dsk_nfree]`, set to 0xFFFF by every mount,
filled by the first `dsk_free_clus_x` walk, and adjusted by `dskw_setfat`
(`diskw.inc:4845`) on a 0 <-> non-0 transition of the entry it rewrites (it
already reads the old straddling word for FAT12). A fixed disk keeps the
count across remounts (`DVK_DRV` cannot be swapped, SPEC.md 18.9.1).
`dsk_free_clus_x` answers the word when it is valid.

Bytes: **~+20 resident**; buys ~105 ms + FAT re-reads per DFREE on a 20MB
disk, and the per-mount count on every Disk-window open of a fixed disk.

### histsys-4 (weakness, MEDIUM, now) - the UI task has no scheduling preference, so input waits a full round of workers

`sch_switch`'s scan (`kernel/sched.inc:2477-2502`) is pure round-robin from
`cur+1`. `sch_wake_ui` (`:700-723`) makes task 0 READY but does not move it
up: with N ready workers the press that the mouse ISR queued waits up to N
quanta of 55 ms before the UI task runs at all. §7.3 fixed the *lock* half of
FIELD-NOTES 27; this is the *CPU* half, which SPEC.md 7.3 itself names
("it also does not make the UI task run more often: that is the round-robin
quantum").

Prior art: OS/2 1.x boosted the foreground and input-receiving thread's
priority; AmigaOS ran `input.device` at priority 20 over every application
task; VMS boosted on I/O completion.

**Recommendation.** In `sch_switch`, before the scan: if `[sch_uiwake]` is
set, task 0 is READY and `[fsx_task]` is 0xFF, pick slot 0. `[sch_uiwake]` is
already set by every input ISR through `sch_wake_ui` and spent by
`ui_task .idle` (`ui.inc:1182-1184`), so the boost lasts exactly one pass and
cannot starve workers. ~6 clocks on a switch where nothing is pending.

Bytes: **~+10 `.text`** on the switch path (mind `t_hotpar`'s parity pins).

### histsys-5 (rethink, MEDIUM, now) - every `int 13h` freezes every task

`dsk_xfer` raises `[sch_lock]` across every BIOS transfer
(`kernel/disk.inc:1462`), and SPEC.md 18 says only "task switching is
paused". A floppy call is ~400 ms, a whole-file read is several; for all of
it no worker runs - not a drawing worker, not a network worker, not the sound
driver's refill/drain tasks (SPEC.md 34.5 makes those TASKS). The protection
it buys is narrow: only the UI task may call int 13h (SPEC.md 18, §20.6
rule 7), `mem_pinseg` already pins the destination, compaction is UI-task
only (§66.3.1), and `sch_switch` already refuses to park a foreign SS
(`sched.inc:2411-2419`), which is the SeaBIOS stack-hop case.

Prior art: OS/2 1.x and NetWare hooked the AT BIOS's `int 15h` AH=90h/91h
"device busy / interrupt complete" calls so the caller blocks and others run;
DISK-CPU-PLAN.md 7.2 names the same hook as an aside. The XT ROM never calls
it but polls IRQ6's flag with IF=1, so on the XT simply leaving switching
enabled lets workers use the wait.

**Recommendation.** Release `[sch_lock]` across the BIOS floppy `int 13h` on
the UI task, keep it for driver paths, hibernate, kern_mega and the debug
door. **Two hazards must be closed first:** `[cur_inxfer]` (`CURXFER_ON`,
`disk.inc:2000`, mouse.inc:55) lets the mouse ISR draw the arrow while
the TRANSFERRING task owns the screen - with switching live, a worker taking
`gfx_lock` must clear or override it; and the retry path's `dec/inc` of
`[sch_lock]` (`disk.inc:2030-2035`) becomes redundant. On an AT add the
`int 15h` AH=90h hook to `task_sleep` the UI task until AH=91h. Measure on
MartyPC first (a worker's frame rate during a 16 KB read) - this is a design
decision that wants one number.

Bytes: **~+20 to +40 `.text`** (+~25 more for the AT hook); listed as an
aside in DISK-CPU-PLAN 7.2, not as a row.

### histsys-6 (weakness, LOW-MEDIUM, now) - keyboard input is outside the event ring, so key/mouse order is lost

`ui_task` polls `int 16h` once per pass (`ui.inc:120-245`, `.typed` at
`:177`) and then drains the whole mouse ring (`:224-233`, `.evloop` at `:408`). Keys and clicks arriving inside
one pass are dispatched keys-first, and only ONE key per pass. Under
starvation (a drawing worker, a disk transfer) a user who clicks window B
and types sees the first keystroke go to window A (the old front) and the
rest to B. SPEC.md 10 states the polling as a fact, not a decision.

Prior art: the Mac Event Manager queued `keyDown` beside `mouseDown` in one
time-ordered queue; Windows 3.x's system queue held both, in order, before
splitting into application queues.

**Recommendation.** `kbm_isr` already runs after the ROM's int 09h has
enqueued (kbddiag.inc:181 "after the ROM's int 09h returned to kbm_isr"):
have it push a payload-less `EVT_KEY` (type 5) marker whenever the BIOS
buffer tail moved. The drain then fetches `int 16h` at the marker's place in
the ring, so order is exact and translation stays the BIOS's. Keep today's
poll as the fallback for a key with no marker (a dropped marker under a full
ring), so nothing is lost. `EVT_KEY` is kernel-internal: no ABI change.

Bytes: **~+25 `.text`**, and one ring slot per key under load.

### histsys-7 (size, MEDIUM, now) - each driver class keeps a 36-byte copy of a union service table

`drv_svc resb DSV_SIZE*(DRVC_MAX-3)` (`kernel/driver.inc:6173`) is
7 x 36 = **252 bytes of resident `.bss`** on kern_big. `DSV_*`
(`drivers/os88drv.inc:294-410`) is the UNION of every class's services -
FM, STREAM, TONE, TIERS for sound; BLK, GEOM for disks; FS for redirectors;
seven Control Panel page procs; PKGCALL - so most of every copy is zeros.
Only the SOUND copy is read by name on a hot path
(`[drv_svc+DSV_TICK]` etc.: 20 sites, all `CAPS/FM/RELINST/STREAM/TICK/TIERS/TONE`);
every other class is reached through `drv_cls_svc_x` (`:1929-1958`, ~14
callers). `DSV_FS` is already a pointer read through the driver's segment
(`:2832`), which proves the shape works.

Prior art: the Mac unit table holds a handle to the driver's DCE and reads
the driver's own header; nothing is copied. DOS reads the device header in
place.

**Recommendation.** Keep the copy for class 1 (the tick reads it at IRQ0).
For every other class keep only the offset `DRVV_ATTACH` returned in SI and
read fields as `[es:di+DSV_*]` with ES = the published segment
(`drv_fptrN+2`). `drv_cls_svc_x` grows the ES load; its ~14 callers each add
an `es:` prefix. No driver changes: the table the driver hands back is the
same table. Works for HMA drivers (segment FFFF) and kern_mega (a selector).

Bytes: **-216 `.bss`, +~40-60 `.text`/`.cold`; net ~-150 resident on
kern_big**, ~-40 on kern_small (DRVC_MAX 5: two copies). Related: ABI-BREAK-LIST
D1 (renumbering for the index) and D2 (superseded) - this is the alternative
that needs no renumber.

### histsys-8 (rethink, MEDIUM, flagday DRV_VER) - the kernel-driver block/file ABI wants a parameter block

There are three dispatch bodies - `drv_svc_call_x`, `drv_blk_call_x`,
`drv_fs_call` (and `drv_fs_hi` on kern_mega, `driver.inc:2687-2760`) - and
their banners say why: "the disk ABI spends AL, AH, BX, CX, DX and SI on the
transfer itself, so there is no register left to say which class this is"
(`:2577-2581`), so the class arrives in `[drv_blkcls]` and the far pointer is
staged in `[drv_blkfp]/[drv_blkseg]`. The sound path lost two arguments to the
dispatcher before the gates caught it (`:2525-2535`). Errors come back as
CF + AX, in whatever code space the verb chose.

Prior art: DOS block devices take one request header at ES:BX (length, unit,
command, status word, then per-command fields); the Mac Device and File
Managers take one `ParamBlockRec` in A0 with `ioResult` and an
`ioCompletion` pointer - which is also how the Mac got asynchronous I/O for
free.

**Recommendation.** At the next `DRV_VER` bump (drivers only; no package
changes, so not `PKG_FMT`), make `DSV_BLK` and the `FSV_*` verbs take ES:BX
-> a request block {verb, class/unit, status, LBA/count or name/offset,
buffer far ptr, completion word reserved = 0}. One dispatcher replaces three,
the staging words go, the status field is the one error channel, and the
reserved completion word is where async I/O goes later without another bump.
Package-facing `OSAPI_FILE_*` registers are untouched; the kernel builds the
block.

Bytes: **kernel ~-60 to -120 resident** (two dispatch bodies, `drv_blkcls`,
the staging pair, `drv_fs_hi`'s `dfh_*` on kern_mega), drivers +bytes on
their own disks. ESTIMATED.

### histsys-9 (rethink, MEDIUM, flagday) - thirteen callback words in every window record, set by ten-plus cells

kern_big's `W_PAINT, W_ONKEY, W_ONCLICK, W_ONSIZE, W_ONMOUSEUP, W_ONDRAG,
W_ONTIMER, W_ONSZ, W_ONWK, W_ONCL, W_ONRC, W_ABOUT, W_ONWH`
(`kernel/wm.inc:24-131`) are 26 bytes x `MAX_WIN` 12 = **312 bytes of
`.bss`**, nearly all constant for a window's life, each with a setter cell.
ABI-BREAK-LIST K17-3 proposes folding the setters into one indexed cell.

The new argument: the STORAGE need not be in the kernel at all. Windows 3.x
put one `lpfnWndProc` in the class and dispatched by message number; GEOS
kept a class method table in the object's own block. Here the package
dispatcher already exists (`PKG_DISP`, `call bp`), so the record needs one
word: **`W_CLASS`, a near pointer into the package's own image to a table of
handler offsets**, read at dispatch as `[W_SEG:W_CLASS + n*2]`. `W_ONCL`'s
one runtime change (retired by `.cl_slot`, `ui.inc:886-893`) becomes a flag bit.
Kernel windows get tables in `.cold`.

Bytes: **-264 `.bss` on kern_big (-180 kern_small), ~-60 of setter cells,
+~150-180 `.cold` of kernel window tables, +~8 at the dispatch; net ~-150 to
-250 resident**, ESTIMATED. Listed as K17-2/K17-3; this is a deeper cut of
the same row and the two should be decided together.

### histsys-10 (rethink, MEDIUM, flagday contract) - repaint is synchronous and nested inside whoever caused the damage

`wm_hide`/`wm_destroy`/`wm_show`/`wm_resize` call `wm_paint_dmg` immediately
(`wm.inc:3166-3168` for destroy), which runs every uncovered window's
`W_PAINT` inside the caller - inside another package's callback, on any
task's stack. `wm_pkgcall`'s banner calls this re-entrancy "by construction"
(`:389-394`), `WM_PKGD_MAX` (8, `:178-186`) exists only so the compactor can
tell which regions have frames standing in them, and finding 1 is this on a
worker. Two damages in one handler are two paint passes.

Prior art: the Mac accumulated damage into each window's `updateRgn` and
delivered one update event when nothing else was pending; Windows 3.x
accumulated an update region and synthesised `WM_PAINT` only when the queue
was empty. Neither ever ran application B's paint code inside application
A's handler.

**Recommendation.** Not now - a package that calls `OSAPI_WM_SHOW` and draws
into the new window expects the paint to have run, and that is contract. At a
flag day: accumulate damage into a short rect list (the `wm_clip_tab`
machinery, `wm.inc:6740-6870`, already splits and adds fragments) and paint
it at the top of the UI pass. Finding 1 is the no-ABI subset that fixes the
dangerous case today.

Bytes: **~+60 to +120** for the accumulator and the pass, against the
nest-tracking simplification it allows. ESTIMATED, unmeasured.

### histsys-11 (weakness, LOW, now) - `OSAPI_WM_WAKE` writes through an unvalidated BX from ISR and worker context

`wm_wake` (`wm.inc:2343-2369`) stores `[bx+W_WKQ]` and queues BX; the later
`evq_mup`/`wm_wake_disp` write through the queued value again
(`events.inc:231-236`, `wm.inc:2410`). The SDK says "BX is trusted"
(`apps/os88api.inc:1726`, for `MEMC_POST`). A stale window pointer from a
worker is a kernel-DS scribble at a moment no lock is held.

Prior art: Windows 3.1's headline robustness change was parameter validation
at the USER/GDI door (every HWND checked with `IsWindow`), after 3.0's UAEs.

**Recommendation.** `wm_isrec` (`wm.inc:11672-11678`) is the range-only
fence and costs three instructions; call it at `wm_wake`'s entry and at
`MEMC_POST`, refusing CF=1. It confines a bad BX to the window table rather
than proving it a record, which is the 80/20 for an ISR-callable door.

Bytes: **~+6 to +12 `.text`**.

### histsys-12 (rethink, LOW-MEDIUM, flagday DRV_VER + SYSTEM.CFG) - the set of drivers is compiled into the kernel

`drv_tab` (`driver.inc:1379-1600`, 16-byte rows), one filename string per
driver (`:1179-1226`), one title per driver instantiated twice
(`DRV_TITLES`, `:1237-1257`) and a `SYSTEM.CFG` bit per row (`drv_cfgbit`)
mean that a new driver is a kernel change, and the rows' comments are a
history of indices that cannot move. DOS's `DEVICE=` chain and the Mac unit
table were open-ended; a third party could ship a driver.

**Recommendation (record-weight).** At a flag day, let the boot overlay scan
`SYSTEM\*.DRV`, take the class and a title from the driver header (DRV_VER
bump), key `SYSTEM.CFG` by an 8-byte name rather than a row bit, and keep in
the resident kernel only the `DRVC_MAX` publication slots - the unit table.
The Drivers page (CTRL.DRV, a module) reads titles off the files when it
opens.

Bytes: **~-200 to -350 resident** (rows, names, titles), **+~120 in the boot
overlay** (scan). ESTIMATED. Not in ABI-BREAK-LIST.

### histsys-13 (consistency, LOW, flagday) - three representations of "where I am in a file"

Whole-file calls take a name; `OSAPI_FILE_READ_SEQ` takes the name AND a
16-byte caller cursor at ES:DI with the buffer at DX:BX; `OSAPI_FILE_WRITE_SEQ`
takes the name AND a token in DI with the buffer at ES:BX
(`apps/os88api.inc:6104-6160`). Both stream forms re-seed silently from the
name, which is the strength in Strength 10. They are two answers to one
question with opposite buffer conventions.

Prior art: the Mac File Manager's FCB lived in the system heap but the
caller's parameter block carried `ioRefNum` and `ioPosOffset` - one shape
for both directions.

**Recommendation.** At the flag day that takes C2-C4, make the write stream
take the same 16-byte caller cursor as the read stream (the token becomes a
field of it) and the same buffer register pair. Bytes ~0 to -30, unmeasured.
Listed neighbour: ABI-BREAK-LIST C2-C4.

### histsys-14 (consistency, LOW, record) - four error-code spaces and one text door

`FERR_*` 0..10 (`kernel/diskw.inc:58-68`), `DRVE_*` 0..9 with its own string
table (`driver.inc:2104-2125`), loader `LD_*` codes (`fm_errtab`, toast.inc
:320), and verb-local codes in the FSV/sound ABIs. `OSAPI_FERR_TEXT` answers
only the first. The Mac had one `OSErr` space across every manager; DOS 3+
had extended error codes with a class and a suggested action (int 21h
AH=59h). Worth recording because finding 8's status field is where a single
space would naturally be born; not worth a flag day of its own.

### histsys-15 (hazard, LOW, now) - `OSAPI_TASK_SLEEP` with AX >= 0x8001 returns at the next tick

`task_sleep` stores `ticks + AX` (`sched.inc:1155-1159`) and the wake scan
decides with the sign of `ticks - T_WAKE` (`.wake`, `:2362-2370`). For AX above 32,768
the difference is already non-negative and the task wakes on the next tick.
The SDK line (`apps/os88api.inc:344`) promises "AX = ticks to sleep" with no
bound. `toast_show` clamps exactly this case (`toast.inc:216-224`).

**Recommendation.** Document the 32,767-tick (30-minute) ceiling in the SDK
line, or clamp in `task_sleep` (`or ah, ah / jns / mov ax, 0x7FFF`).
Bytes: 0 (doc) or **+5**.

### histsys-16 (consistency, LOW, now) - the kernel holds two contradictory rules about `.bss` at boot

`events.inc:61-65` deletes `evq_init` because ".bss ARRIVES ZEROED" (the
image's inter-section padding, checked once on the built binary);
`t_bsssentinel.py` and SPEC.md 12.8.5.1 say the same; `vga12.inc:304-306`
says "it arrives zeroed". `sched_init` (`sched.inc:341-363`) stores zeros
"by hand: nothing clears .bss at boot", SPEC.md 8 says "nothing clears .bss
at boot (`-f bin` puts no bytes on disk and the boot sector does not blank
the region)", and a dozen modules repeat that (`band.inc:828`, `files.inc:484`,
`instance.inc:1440`, `snd.inc:33`, ...). Both cannot be the rule. The cost of
the ambiguity is bytes (defensive zero stores in resident init paths) and a
class of bug (`fsx_cur`, 12.8.5.1).

**Recommendation.** Pick the zeroed rule, which the code already relies on,
and make it a gate: extend `t_bsssentinel.py` to assert the `.bss` span reads
all-zero in `kernel.bin` on all four budgeted kernels (it would have to
follow how each one's stage-2 read covers `.bss`). Then the resident
zero-stores can go; the ones in `.ovl`/`.boot2` (sched_init's) cost nothing
and may stay. Bytes **0 to ~-20 resident**.

### histsys-17 (weakness, LOW, now) - the "display context" is eight scattered bytes reset one by one

`gfx_unlock` (`vga12.inc:5149-5268`) kills the clip list, the blit pen and
its polarity byte, `gfx_mono1`, `gfx_dis`, `dsk_batch`, the display context,
the progress widget and the busy pointer - each with its own store, because
they live in different places. That list IS a Windows DC / QuickDraw
GrafPort, implicit, bound to the lock hold rather than to a window, and
`wm_pkgcall` banks two of its fields per callback (`wm.inc:397-417`). R6
(content coordinates, a per-window DC) is the listed fix and is shelved on
budget.

**Recommendation.** Without taking R6: make the zero-resetting fields
adjacent (`wm_clip_n`, `gfx_mono1`, `gfx_dis`, `dsk_batch`, `gfx_b1adp`) so
`gfx_unlock` clears them with two or three word stores, and name the block
`gfx_dc` so R6 has a home when it comes. Bytes **~-4 to -8**. Listed as R6.

## Rethinks

The four that would change the shape of the system, in the order they should
be decided:

1. **Teardown and repaint belong to the UI task** (histsys-1 now,
   histsys-10 at a flag day). Every peer of this OS ran a window's paint on
   the window owner's thread and nowhere else; os8088 runs it on whatever
   stack caused the damage. Fixing the dying-worker case costs ~25 bytes and
   no ABI. Deferring all damage is the larger, contract-changing step.
2. **The kernel-driver ABI becomes a request block** (histsys-8), with the
   copied service tables gone (histsys-7) and, optionally, discovery instead
   of a compiled-in row table (histsys-12). Together: a `DRV_VER` flag day,
   no `PKG_FMT` change, ~-400 to -600 resident across the three, and the
   async-I/O door left open for free.
3. **The window record keeps a class pointer, not thirteen handlers**
   (histsys-9), decided together with K17-2/K17-3.
4. **The disk wait stops freezing the machine** (histsys-5), plus the
   scheduling preference for input (histsys-4). Both are "now" and small;
   both want one MartyPC number before they are built.

## Prior art

| subsystem | DOS | Win 3.0/3.1 | OS/2 1.x PM | Mac System 5-7 | GEOS | AmigaOS | os8088 | adopt cheaply |
|---|---|---|---|---|---|---|---|---|
| input queue | BIOS buffer only | one system queue (keys + mouse, ordered), per-app queues | per-thread queues | Event Manager: one queue, keyDown beside mouseDown | input via flow object, ordered | input.device stream, priority 20 | mouse ring + polled keyboard | keyboard marker in the ring (histsys-6) |
| idle | spin | spin, WIN386 int 2Fh 1680h | blocked threads | `WaitNextEvent` sleep | idle loop | Wait() on signals | **blocks + halts, lost-wakeup-proof** (better) | - |
| scheduling | none | cooperative | pre-emptive priorities, foreground/I-O boost | cooperative (MultiFinder) | pre-emptive threads | pre-emptive priorities | pre-emptive round-robin, no priority | input boost (histsys-4) |
| paint | n/a | update region, WM_PAINT when queue empty | WM_PAINT per thread | updateRgn, update event | MSG_VIS_DRAW via queue | none (app draws) | **synchronous, nested, any stack** | deferred teardown now (histsys-1), update list later (histsys-10) |
| clipping | n/a | DC clip region | HPS regions | visRgn/clipRgn regions | window regions | layers.library ClipRects | rect-list visRgn, 16 frags (good fit) | - |
| drawing state | n/a | DC per GetDC | presentation space | GrafPort per window | GState per window | RastPort per window | implicit, bound to one global lock | name the DC block (histsys-17), R6 later |
| callbacks | n/a | WndProc + message number, MakeProcInstance | WndProc | event loop in app | method tables per class | message ports | **slot per handler, package dispatcher** (dispatcher better; slots costly) | class table (histsys-9) |
| drivers | DEVICE= chain, request header at ES:BX | .DRV DLLs, exported ordinals | PDD/VDD, request packets | unit table + DCE, ParamBlock, async + ioCompletion | driver libraries | exec devices, IORequest + reply port | closed row table, register ABI, per-class table copies | request block (histsys-8), read in place (histsys-7), discovery (histsys-12) |
| disk wait | busy | busy (WIN386 hooks int 15h 90h) | int 15h 90h/91h, thread blocks | async PBRead | async | async DoIO/SendIO | **every task frozen** | release the lock (histsys-5) |
| files | handles (SFT), leak on crash | handles per task | handles | refNum + FCB, PB | VM files | Lock/FileHandle | **stateless by name** (better at death); 2 cursor shapes | unify the cursor (histsys-13) |
| free space | DPB free count | DOS's | FSD cached | VCB free blocks | - | InfoData cached | **recount every ask** | cache it (histsys-3) |
| media change | change line, else 2-second guess | DOS's | - | disk-inserted event, named volume prompt | - | disk change interrupt | **motor-timeout fact** (better) | - |
| fatal errors | int 24h critical error, abort/retry | UAE box | trap screen | SysError bomb box with ID | system error box | Guru/Recoverable alert | one panel (stack overflow); no int 0 | system-error panel + int 0 (histsys-2) |
| handle validation | none | none in 3.0, full in 3.1 | full | none | handles checked | none | 5 sites fenced | fence the ISR-callable doors (histsys-11) |

What os8088 does better than all of them, systemically: the idle model, the
sample/promise distinction in the event ring, the self-healing posted-work
byte, the physical media-change test, and stateless files. What all of them
did better: run paint and teardown on the owner, and have one fatal-error
path.

## Doc/code mismatches

Each row: file:line, a short exact quote, what the code does, proposed text.

1. `kernel/ui.inc:5` - "drains one event from the system queue" -> the pass
   drains up to `EVQ_CAP` records (`ui.inc:224-233`, SPEC.md 10.2).
   Proposed: "drains the system queue (up to EVQ_CAP records a pass, SPEC.md
   10.2)".
2. `kernel/ui.inc:1255` - "evq_push DROPS silently when the ring is full" ->
   a full push discards the OLDEST input (`events.inc:95-116`). Proposed:
   "a full ring discards its OLDEST input (SPEC.md 10.1), so a release can be
   lost, and ui_drag, ui_grow and fm_drag each carry the same fallback".
3. `kernel/events.inc:78` - "drops the event silently when the queue is full"
   -> drops the oldest unless the oldest is an EVT_WAKE, then the newest
   (`:95-116`). Proposed: "a full ring loses its OLDEST record - or this one,
   when the oldest is an EVT_WAKE (SPEC.md 10.1)".
4. `kernel/events.inc:17` - "payload c (unused so far)" -> EV_C is the birth
   tick of every mouse record (`:20-22`, read at `ui.inc:465`). Proposed:
   "word: payload c (mouse events: birth tick)".
5. `kernel/wm.inc:15` - "window record, 20 bytes, fixed offsets" ->
   `WIN_SIZE` is 74 on kern_big and 65 on kern_small (`:121`, `:126`).
   Proposed: "window record, WIN_SIZE bytes (74 kern_big, 65 kern_small),
   fixed offsets".
6. `kernel/wm.inc:3068` - "then repaints all" -> `wm_destroy` repaints the
   vacated rect through `wm_paint_dmg` (`:3162-3168`). Proposed: "then
   repaints what the window vacated (wm_paint_dmg)".
7. `kernel/wm.inc:2353-2354` - "is banked at entry and dead on both arms" /
   ".full goes popf/stc/.out: pop si/pop ax/ret." -> `wm_wake` banks SI only
   (`:2344`, `:2366-2367`). Proposed: "no bank: AX is not written on either
   arm - .full goes popf/stc/.out: pop si/ret".
8. `kernel/vga12.inc:301-302` - "Four writers in the tree and no fifth: :5303
   (1), :5384 (0), sched.inc" -> the takes are at `gfx_lock .take`
   (`:5120-5125`) and `gfx_unlock .rel` (`:5262`), the KFZ pair at
   `sched.inc:2098/2114`, and kern_mega adds three (`mega.inc:2491`, `:7569`,
   `:7582`). Proposed: "Writers: gfx_lock's .take (1), gfx_unlock's .rel (0),
   KFZTRACE's bank-and-force in sch_isr, and on kern_mega mega.inc's fault
   paths - each stores 0 or 1, never a count."
9. `kernel/toast.inc:400-401` - "gfx_unlock writes owner FIRST then flag" ->
   both routines write flag and owner in ONE word store (`vga12.inc:5120-5125`,
   `:5262`). Proposed: "gfx_lock and gfx_unlock each write the pair in one
   word store, so no torn pair is ever visible".
10. `kernel/sched.inc:104` - "256 is 1.8x that" -> `SCH_STACK` is 384
    (`:93`). Proposed: "384 is 2.7x that".
11. `kernel/sched.inc:111-115` - "slice's base with a byte swap; that path
    is still there for 256" -> `sch_switch` reads `sch_stkbase[n]`
    (`:2442`); the derivations are gone (`:2785-2790`). Proposed: "It used
    to have to be 256 exactly, because sch_switch derived the slice base
    from the slot index; it reads sch_stkbase now (SPEC.md 8.6), so any
    even size in SCH_PARTITION is legal".
12. `kernel/sched.inc:17-18` - "SCH_STACK-byte stack in .lowbss" -> slices
    are sized per `SCH_PARTITION` (`:174-236`). Proposed: "each with its own
    slice of sch_stacks in .lowbss, sized by SCH_PARTITION (SPEC.md 8.7)".
13. `kernel/sched.inc:288` "kmain:4170 is its ONE caller", `:805`
    "kmain:4182", `kernel/wm.inc:501` "kmain:4291" -> the calls are at
    `kernel/kernel.asm:5686` (`OVBCALL sched_init`) and `:5698`
    (`OVLCALL sch_idle_start`). Proposed: drop the line numbers ("kmain is
    its ONE caller").
14. `kernel/sched.inc:360` - "nothing clears .bss at boot, so" -> contradicts
    `events.inc:61` and SPEC.md 12.8.5.1 (histsys-16). Proposed: ".bss
    arrives as the image's zero padding (SPEC.md 12.8.5.1), but this body is
    boot-overlay code, so the explicit stores cost no resident byte and are
    kept".
15. `kernel/diskw.inc:480` - "Answered entirely from the resident FAT
    snapshot - NO DISK I/O" -> on a FAT16 partition the FAT is windowed and
    `dsk_free_clus_x .f16` faults sectors in through `dsk_fat_ofs_x`, "which
    does disk I/O" (`disk.inc:6454-6462`). Proposed: "Answered by counting
    the FAT: no I/O on a floppy, whose FAT is wholly resident, but FAT
    re-reads through the window on a FAT16 partition - and O(clusters) of
    CPU either way".
16. `kernel/diskw.inc:496` - "clusters * spc <= TotSec16 <= 5,760)" -> a
    hard-disk or BIGVOL partition exceeds 16 bits of sectors; the code is
    32-bit and correct. Proposed: "(free clusters * spc fits 23 bits on
    every volume this kernel mounts)".
17. `apps/os88api.inc:1106` - "NO DISK I/O - AND THAT IS NOT THE SAME AS"
    -> as 15. Proposed: "NO DISK I/O ON A FLOPPY, FAT RE-READS ON A HARD
    DISK - AND EITHER WAY NOT CHEAP".
18. `apps/os88api.inc:4007` - "the root directory is full" -> the kernel's
    `FERR_DIRFULL` is "the current directory has no free slot"
    (`kernel/diskw.inc:65`); subfolders exist. Proposed: "the current folder
    has no free entry".
19. `SPEC.md:38441` - "the answer comes out of the resident FAT snapshot and
    no sector is" -> as 15. Proposed: "the answer is a count of the FAT; on
    a floppy no sector is read, on a FAT16 partition the 9-sector window
    re-reads the FAT as it goes".
20. `SPEC.md:10687` - "exactly **two** routines raise it" -> `sch_lock` is
    raised in `disk.inc` (5 sites incl. `dsk_free_clus_x`), `diskw.inc:6347`,
    `fsx.inc:2244`, `hiber.inc:1843/3039`, `sch_rhook` (`sched.inc:610`) and
    ~12 kern_mega sites. Proposed: "Outside the scheduler, it is raised
    around every int 13h (§18), around FAT-window walks that bank its
    segment, by the clip door OSAPI_SND_PLAY (§34.4), by fsx and hibernate
    brackets, by the rate hook (§53.2.2) and by kern_mega's descriptor
    edits - always with the same meaning".
21. `SPEC.md:12353` - "and **7 on `kern_small`**" -> `MAX_TASKS equ 5`
    (`kernel/sched.inc:47`). Proposed: "and **5 on `kern_small`** (§11.102;
    it was 7 at D1)".
22. `SPEC.md:12372-12374` - "one 8-bit multiply and a" ... "byte swap kept
    under `%if SCH_STACK == 256`" -> a table read of `sch_stkbase`
    (`sched.inc:2444-2449`). Proposed: "one table read (sch_stkbase[n]) and a
    compare - no arithmetic, for any slice size".
23. `SPEC.md:12400` - "nothing clears .bss at boot (`-f bin` puts no" ->
    contradicts §10 and §12.8.5.1 (histsys-16). Proposed: "`-f bin` emits
    nothing for .bss and what lands there is the image's zero padding
    (§12.8.5.1); sched_init stores them anyway because it is boot-overlay
    code, where a store costs no resident byte".
24. `SPEC.md:12420` - "(kernel/sched.inc:422–423 and :434)" -> the frame is
    built by `sch_frame` (`sched.inc:1075-1107`). Proposed: "(sch_frame,
    kernel/sched.inc)".
25. `SPEC.md:18117` (§10.1 snippet) - "cmp word [evq_count], EVQ_CAP" -> the
    three indices are bytes widened with `cbw` (`events.inc:95-116`).
    Proposed: replace the snippet with the byte form
    (`cmp byte [evq_count], EVQ_CAP` / `mov al, [evq_head]` / `add al,
    EVQ_RECSIZE` / `and al, EVQ_MASK` / `dec byte [evq_count]`).
26. `SPEC.md:18181` - "and `rep movsw` reads it through DS" -> the copy is
    four unrolled `movsw` with no `rep` (`events.inc:267-270`); the
    prefix-loss rule applies to a restarted REP string op, so the bank is
    chosen for size (4 bytes against four `ss:` prefixes). Proposed: "the
    four `movsw` read it through DS, which is BANKED: four `ss:` prefixes
    would be four bytes more, and a prefix is lost when a REP string op
    restarts after an interrupt, so banking is also the shape that survives
    the copy ever growing a `rep`".
27. `SPEC.md:18271` - "fairness (§7), still open" -> §7.3's fairness debt
    landed (`vga12.inc:5082-5136`). Proposed: "that is the round-robin
    quantum (§7.3 closed the lock half)".
28. `SPEC.md:34314` - "Transfers go **one sector per int 13h" -> `dsk_xfer`
    issues runs of up to 128 sectors (`[dsk_run]`, `disk.inc:1998`,
    §18.91), and the table under it says so. Proposed: "Transfers go in
    RUNS - as many sectors per int 13h as the track, the 64KB DMA page and
    the buffer allow (§18.91), falling back to one sector per call after
    three failures at a run length".
29. `SPEC.md:28270` - "close box → **quit**: gfx_lock, `app_close_win` BX" ->
    a press on a box ARMS and lights it; the release fires it
    (`ui.inc:671-681`, `:318-405`, §13.5). Proposed: "close box ->
    ARM (§13.5): the press records `[ui_armw]`/`[ui_armr]` and lights the
    box; the matching release, if it lands on the same box, runs
    `app_close_win` under the lock".
30. `SPEC.md:134621` - "#### 75.3.3 `OS88UI_ADANGER`" sits after
    `## 76.` (`:134585`) -> structural. Proposed: move the 75.3.3 block
    above the `## 76.` heading.
31. `SPEC.md:100213` - "#### 50.6.5 `MEM_P_VIEW`" sits inside `## 51.`
    (`:100196`) -> structural. Proposed: move it back after §50.6.4
    (`:100059`).

## Open questions

- **histsys-1's real depth.** Nobody has measured a worker's high water
  *during its own teardown*. One `tools/stkwater.py` reading of Bounce closed
  over a second window settles whether this is a margin problem today or a
  latent one.
- **Why `[sch_lock]` across `int 13h` on the XT?** SPEC.md 18 states the rule
  but not the failure it prevents beyond "nothing switches underneath one"
  (SPEC.md:38868, a stack-depth argument for a harness on a worker stack).
  If there is a BIOS-timing reason (head-settle or motor loops on a specific
  ROM) it should be written down before histsys-5 is attempted.
- **Does the `.bss`-zero rule hold on kern_small and kern_mega?** events.inc
  checked kern_big's binary once. histsys-16's gate would answer it for all
  four.
- **histsys-9 and runtime handler changes.** Which shipped packages change a
  handler word after `OSAPI_WM_CREATE` (modes, wizards)? Each would need a
  second table or a self-dispatching entry; a grep of `OSAPI_WM_ON*` call
  sites outside entry procs decides the real cost.

## Verification

Adversarial pass, 2026-11-08. Every hazard and every high or medium finding was
re-read in the code. Low findings 6, 11, 15, 16 and 17 were spot-checked;
12, 13 and 14 are unchecked. No finding is refuted. The corrected fields are
in `data/histsys.json`.

Findings:
- **histsys-1** confirmed (PLAUSIBLE, unmeasured). The close box hides the window on the UI task. `inst_task_die` then runs gfx_lock, `wm_destroy`, the `.chrome` arm (`wm_dock_under` + `menu_draw_bar`, which draws text), `wm_destroy_seg` and `desk_drop_x` on the worker's own slice. SPEC 8.7.4 measured a covered Timer's text chain at about 90 bytes plus a 32-64 byte floor, so 128 is plausibly over. Eight 192-class packages, not nine.
- **histsys-2** corrected. The missing handler is real. On the IBM 5150/5160 ROM, though, the int 0 stub masks the 8259, so a divide overflow is a dead machine (docs/TESTING.md, tests/int0sweep.py). Only GLaBIOS carries on with a garbage quotient. The 286+ re-execute hang stands.
- **histsys-3** confirmed. No cached count exists. The FAT16 arm loads the window once per FAT sector. A cache must also be dropped on refat/remount and must not cover a redirected volume.
- **histsys-4** corrected: severity medium -> LOW. The scan is pure round-robin. SPEC 7.3 already measured `QUANTUM=` at 16-61 ms against 37-70 with one worker, "inside the noise". Only the N-worker case is unmeasured.
- **histsys-5** confirmed (rethink).
- **histsys-6** confirmed (spot-check).
- **histsys-7** corrected. `drv_publish` zeroes all 18 cells and copies only as far as the driver's `DRV_H_DSV` length. An in-place read must therefore length-check each non-sound cell, or else a minimum table length becomes a DRV_VER rule; without either it reads a short table's name string. The copy is also a snapshot. Net saving re-estimated at about -90 to -140 kern_big and about -36 kern_small (ESTIMATED).
- **histsys-8** confirmed (flag-day rethink, bytes ESTIMATED).
- **histsys-9** corrected. kern_small has 10 handler words x MAX_WIN 6 = 120 bytes, so it saves about -108, not -180. The kern_big arithmetic (13 x 12 = 312) is right.
- **histsys-10** confirmed (flag-day rethink).
- **histsys-11** confirmed (spot-check).
- **histsys-12** unchecked.
- **histsys-13** unchecked.
- **histsys-14** unchecked.
- **histsys-15** corrected. `[ticks]` is incremented before the wake scan, so AX = 0x8001 still sleeps its full length. Early wake starts at AX >= 0x8002.
- **histsys-16** confirmed. `ticks`/`sch_*` are `.bss` and `sched_init` is boot-overlay code. Whether kern_small and kern_mega share the zero padding is still the open question above.
- **histsys-17** confirmed (spot-check).

Doc mismatches (31): every quote was found verbatim. None is refuted.
- **Carried in `data/histsys.json` (15 fixes):**
  - ui.inc:1255
  - wm.inc:2353 (the stale `pop ax` bank)
  - toast.inc:401 (both writers use one word store)
  - sched.inc:104
  - sched.inc:360 (softened to "checked on the kern_big binary")
  - diskw.inc:480
  - os88api.inc:1106 (an abilock-fingerprinted OSAPI contract line, so it is applied with `tools/abilock.py --update`, no bump)
  - os88api.inc:4007
  - SPEC 18.4.5 (:38441)
  - SPEC :12420
  - SPEC :18271
  - the two structural moves, each as a pair of fixes: the `## 76` heading and intro go below §75.3.3, and the `## 51` heading and intro go below §50.6.12. All of §50.6.5-50.6.12 sat under §51's intro, not only 50.6.5.
- **Confirmed but carried by another area's data file:**
  - ui.inc:5 and SPEC close box (ui.json)
  - events.inc:17/78, sched.inc:18/112/288/805, SPEC :10687 / :12353 / :12372 / :12400 / :18117 / :18181 (sched.json)
  - wm.inc:15/501/3068 (wm.json)
  - vga12.inc:301 (gfx.json)
  - diskw.inc:496 (diskwrite.json)
  - SPEC :34314 (diskread.json)

  They are left out here so that no old_text is replaced twice.
- Not proposed: the minimize box line beside SPEC :28270 has the same press/release error as the close box. sched.inc:2533's "1.8x margin" is stale too, for whoever applies sched.json.
