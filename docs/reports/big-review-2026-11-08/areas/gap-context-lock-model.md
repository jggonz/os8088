# One execution-context and lock model: who may run what, on which stack, under which lock, and who paints

Gap review, 2026-11-08. Code read at the review's tree; no build or emulator
run. Line numbers are this tree's. "ESTIMATED" bytes are hand counts of the
instructions named, never a measured span.

## Scope read

- `kernel/vga12.inc` 296-330 (the four lock bytes), 5072-5266 (`gfx_lock`,
  `gfx_unlock`).
- `kernel/sched.inc` 692-723 (`sch_wake_ui`), 895-960 (`task_spawn`),
  1150-1219 (`task_sleep`, `task_exit`), 1860-1890 (`task_yield`'s locked
  arm), 2140-2160 and 2370-2455 (`sch_isr`'s two refusals, `sch_switch`'s SS
  test and canary), 2035-2120 (the KFZTRACE strip), 2985-3010 (`sch_lock`'s
  declaration).
- Every `inc`/`dec`/`mov` of `[sch_lock]` in `kernel/` (grep, 60-odd sites),
  reading each taker: `disk.inc` 888, 1462, 2030-2035, 2433, 6405, 6458;
  `diskw.inc` 6347; `fsx.inc` 2244; `snd.inc` 955-988; `xmem.inc` 640-666;
  `memory.inc` 4040-4103; `hiber.inc` 1843/1985/3039/3446/5205; `mega.inc`
  pool sites; `megados.inc` 302.
- `kernel/mouse.inc` 44-81 (`CURXFER_*`, `CURBAR_*`), 4205-4300 (the ISR's
  draw gate).
- `kernel/instance.inc` 1648-1738 (`app_close_win`), 1903-1947
  (`inst_unwin`, `inst_task_die`), 2165-2215 (`inst_pkg_alive`), 2225-2240
  (`inst_launch_post`).
- `kernel/wm.inc` 389-470 (`wm_pkgcall`), 2343-2369 (`wm_wake`), 3234-3352
  (`wm_lk`, the four doors), 11669-11678 (`wm_isrec`), 11697-11860 (the nest
  stack, `wm_pkgd`/`wm_tcnt`).
- `kernel/ui.inc` 100-125 (the posted-work ladder), 578-600
  (`.content_front`), 810-900 (`.tp_locked`, `.cl_slot`), 1176-1186 (`.woke`).
- `kernel/memory.inc` 940-990 (who compacts/sheds), 2440-2475
  (`mem_in_nest`), 3070-3092 (the driver-image IF=0 move), 4550-4665
  (`mem_regrow`), 4695-4710, 4790-4830 (`mem_free_rec_x`), 4925-5023
  (`mem_avail_lvl_p`).
- `kernel/events.inc` 80-135 (`evq_push`), `kernel/toast.inc` 366-420
  (`toast_now`), `kernel/fprog.inc` 600-640, `kernel/mega.inc` 2480-2720
  (`mega_fkill`).
- `apps/os88api.inc` 3960-3997 (the SDK's worker rules), 6601-6618
  (`OSAPI_WM_WAKE`); `apps/dos/dosw.inc` 5-50 (the DOS window's IVT
  exchange).
- SPEC.md 7 (10651-10707), 7.3 (11619-11660), 20.6 (45921-46140), 29.2
  (67820-67852).
- Area reports `histsys.md` (findings 1, 5, 10, 11), `histmem.md` (hm-1, -3,
  -5), and the data files for `isr` (-2, -6, -7, -12, -16), `ui` (-2, -20),
  `wm` (-7, -12, -20, -R1, -R2), `hiber` (-5), `abi` (-8), `sysmisc` (-8),
  `megacore` (mc-5, mc-15). `docs/plans/ABI-BREAK-LIST.md` and
  `docs/plans/KERNEL-BYTE-QUEUE.md` were searched for context, lock, worker
  and paint rows. Neither has one. R6 (content coordinates, design B, shelved)
  is the nearest.

## Verdict

The kernel has **three real locks**:

- `gfx_lock`, a four-byte RAM semaphore with an owner, a waiter flag and a
  one-debt fairness rule;
- `sch_lock`, a counted pre-emption disable;
- IF=0.

It also has **at least eight ad-hoc context bytes**: `cur_inxfer`,
`cur_barok`, `mem_cp_busy`, `wm_pkgd`/`wm_tcnt`, `sch_rbusy`, `sch_chbusy`,
`fsx_task` and `dsk_batch`. Each was invented to close one field report, and
most are argued correctly at the point of use. What is missing is a model,
not a mechanism. No document says which contexts exist, what each may call,
or in what order the locks nest. Three consequences follow:

1. **Work runs on whatever task caused it.** Instance teardown, and the
   cross-package repaint it triggers, runs on the dying task's stack. That
   one decision is the root of histsys-1, hm-3 and ui-2. It is also the root
   of the "dying worker's repaint" special case in the heap
   (`memory.inc:961-963`), the 13.9.2 re-test in the timer pass, and
   kern_mega killing a faulting worker on its own faulted stack. The UI task
   already drains six kinds of posted work: `inst_launch`, `wm_clreq`,
   `mega_fkq`, the posted compaction, `ui_rebootq` and `wm_tarm`.
   **Teardown is the one that is not posted.**
2. **`sch_lock` is a big kernel lock with six meanings.** SPEC.md 7 says "exactly **two** routines raise it", but there are about forty takers.
   Because it is one byte, it cannot be released for its expensive meaning,
   int 13h. That release is histsys-5. It is feasible on kern_big and
   kern_small if three things change, given in ctx-3. One of them is a real
   bug the release would create: the mouse ISR's "held by us" arm reads
   `[sch_cur]` at interrupt time.
3. **The rules are prose in two lists that disagree, and none of them is
   enforced.** SPEC.md 20.6 rule 7 and the SDK's rule 7 name different
   cells. Rule 4's "no owner field, so no kernel defence" has been false
   since `gfx_lock_own` landed. The lock-order rule "never block under
   `sch_lock`" is obeyed everywhere and written nowhere.

The single rule the brief asks about, *damage is queued, and paint and
teardown run only on the UI task*, is possible. It splits cleanly:

- **The safety half needs no ABI change: teardown on the UI task** (ctx-1,
  about +12 to +28 resident). Rule 7 already forbids workers every
  window-changing call. Once teardown moves, every damage source in the
  machine is on the UI task by construction.
- **The deferral half is a coalescing and stack-depth optimisation, not a
  safety one** (ctx-9, histsys-10, wm-R2). It is honestly a flag day, or an
  opt-in.

An IF=0 bound is achievable for the XT at the 1,200-baud mouse's ~8 ms, but
not at SERIAL.DRV's 9,600-baud ~1 ms while the arrow is drawn inside the
serial ISR. No row in `tests/suite.py` measures IF=0 duration today (ctx-4).

## Strengths

- **`gfx_lock` is already an OS/2 RAM semaphore in four bytes, and a good
  one.** The flag and owner go in one word store inside the cli window
  (`vga12.inc:5124`, `:5262`), so the pair can never disagree. `want`/`last`
  is a one-debt fairness rule that took the press-to-menu latency from
  1.4-14.7 s to 37-70 ms (SPEC.md 7.3). The owner stamp lets three readers
  ask "is it *mine*" rather than "is it held": `toast_now`, `fpg_arm` and
  the mouse ISR.
- **The nest stack is already task-aware.** `wm_nest_pushd`
  (`wm.inc:11735-11754`) records the UI task's frames as a stack and only
  *counts* other tasks' (`wm_tcnt`, the K9 fix). It does that under
  `pushf`/`cli` because the driver service cell is reached from IRQ0. That
  is exactly the per-context bookkeeping the rest of the model lacks.
- **Only the UI task compacts or sheds on kern_big** (`memory.inc:955-965`,
  `:4703-4706`). The comments say why in terms of contexts ("a compaction run
  from another task's stack could move a claim the UI task holds in a
  register"). Of all the context rules, this one is stated and enforced in
  code.
- **`CURBAR_ON` is task-scoped** (`mouse.inc:66-74`: it stores `[sch_cur]+1`
  "because fpg_busy sets it with the gfx lock FREE and IF=1"). That is the
  right shape for any context flag an ISR reads, and it is the model for
  ctx-3's fix.
- **The DOS window's IVT exchange is a complete context discipline in
  package code** (`apps/dos/dosw.inc:11-30`). A program's vectors are in the
  IVT exactly while its own task runs, and that includes a refusing int 13h.
  So a windowed DOS program cannot re-enter the BIOS disk path whatever
  `sch_lock` does.
- **kern_mega's `mega_fkill` already does owner-died cleanup and posted
  teardown.** It releases a lock the faulting task held (`mega.inc:2694-2701`)
  and posts callback faults to `ui_task` (`mega_fkq`, `:2659`). It is the
  existing proof that both halves of ctx-1 and ctx-2 fit the kernel.

## The matrix, as the code has it

### Contexts

| ctx | what | stack | entered by |
|---|---|---|---|
| **U0** | UI task, lock free: top of `ui_task`, entry procs (SPEC.md 20.2), ONWAKE handlers (`memory.inc:961`) | task 0's slice, 1,024 | `ui_task` loop |
| **U1** | UI task, gfx lock held: every window callback but ONWAKE, `ui_bill`'s funnel | same | `gfx_lock` then `wm_pkgcall` |
| **UL** | UI task with `[sch_lock]` raised: `dsk_xfer`, the compactor's plan and moves, a PCM clip, hibernate, the FAT walk, the xmem copy | same | the takers listed under ctx-3 |
| **W0** | worker (a package's §20.6 worker, `OSAPI_DRV_TASK`, the sound refill tasks, the idle task), lock free | its class slice: 128 / 192 / 256 / 384 | `task_spawn` |
| **W1** | worker, gfx lock held | same | `gfx_lock` |
| **WD** | **dying worker**: `inst_pkg_alive .die` -> `inst_task_die` | same slice, **whatever is left of it** | `OSAPI_TASK_ALIVE`; `mega_fkill .worker` |
| **I** | hardware ISR, IF=0: `sch_isr` with its wake loop and `snd_tick` -> `DSV_TICK`, `kbm_isr`, `mou_isr`, card IRQs (SB, NIC, PicoMEM -> `osapi_mouse_feed`) | **the interrupted task's slice**. `mou_isr` alone swaps to `mou_pstack` (MOUPRIV loads SS) | the PIC |
| **H** | `FSXF_RATE` hook, inside IRQ0's sub-tick | the interrupted stack, at most 48 bytes, no kernel calls (§53.2.2) | `sch_rhook` |
| **X** | exclusive fsx bracket: `[fsx_task]` set, `sch_fast` | the owner's | `OSAPI_FSX_RUN` |
| **M** | kern_mega fault or reflection: `mega_fault` -> `mega_fkill` | the faulting stack, then unwound or killed | an exception |
| **F** | fatal panels: `sch_diepanel`, `mega_report`, KFZTRACE's strip | any | forced. They clear `gfx_lock_flag` themselves |

### Services against contexts

`ok` means legal and done in the tree. `rule` means forbidden by prose only.
`code` means refused in code.

| service | U0 | U1 | UL | W0 | W1 | WD | I | H |
|---|---|---|---|---|---|---|---|---|
| draw `gfx_*`/`font_*` | only via a door that takes the lock (the `wm_lk` four); `fprog` draws unlocked by design | ok | `fprog` only, and `dsk_xfer` drops the lock for it (`disk.inc:2030`) | rule (§7: under the lock) | ok, clip first | **ok, under the lock, and it paints OTHER packages** | arrow only (`mou_isr`); F forces | no |
| take `gfx_lock` | ok | **deadlock** (not reentrant; `wm_lk` is the recursive door) | **livelock** if contended (ctx-8) | ok | **deadlock** | ok | no ("task-level only", `vga12.inc:5074`) | no |
| `task_yield`/`task_sleep` | ok | ok (`wm_anim` holds the screen across it, wm-20) | **returns at once** (`sched.inc:1881`), so a sleep becomes a busy return | ok | ok but rule 3 | `task_exit` | no | no |
| `mem_claim` | ok, + compact, + shed | ok, + compact and shed of others; never its own frame's (`mem_in_nest`) | compactor only | first fit only on kern_big (**code**, `memory.inc:955`); **first fit + shed on kern_small** (ctx-6) | same | `mem_free_rec`, which **writes the global `[mem_pg_floor]`** (`memory.inc:4826`) | never | never |
| file / int 13h | ok | ok | nests | **rule 7, not enforced** | rule | rule | never | never |
| `wm_show`/`hide`/`front`/`resize` | ok (door takes the lock) | ok (door sees it is ours) | no (`wm.inc:3263`) | rule 7 | rule 7 | n/a | no | no |
| `wm_destroy` (repaint) | **no door** (wm-7) | ok | no | rule 7 | rule 7 | **yes**: `inst_unwin` on the worker (`instance.inc:1931`) | no | no |
| `OSAPI_WM_WAKE` / `evq_push` | ok | ok | ok | ok | ok | ok | ok, **if SS = LOW_SEG** (ctx-5) | no (no kernel calls) |
| `toast_show` | staged | drawn now if `own == 0 == cur` (`toast.inc:393-398`) | staged | staged | staged | `toast_owner_gone` | no | no |
| `mod_drop` | ok | from inside the image: the banner forbids it | - | - | - | **yes**: `desk_drop_x` (`instance.inc:1938`), hm-3 | - | - |

### Lock nesting as the code obeys it

- `gfx_lock` then `sch_lock`: **allowed**, and it is the common case (a
  callback's file write).
- `sch_lock` then `gfx_lock` (blocking): **forbidden**. It is a livelock
  under contention, because a yield under `sch_lock` resumes the caller. The
  only statement of this is `wm.inc:3263`: "never inside dsk_xfer's
  [sch_lock]".
- `sch_lock` then a yield or sleep: a no-op. The compactor's park therefore
  **drops** `sch_lock` round `inst_park_req` (`memory.inc:4101-4103`), and
  `dsk_xfer` drops it round `fpg_busy` (`disk.inc:2030-2035`). Both are the
  rule obeyed by hand.
- IF=0 then anything that blocks: never. `task_exit` enters `sch_switch` at
  IF=0 by design (`sched.inc:1189-1219`).
- IF=0 inside `sch_lock`: allowed. A driver image moves that way
  (`memory.inc:3072`).

### What each lock actually means

- **`gfx_lock`.** The screen, the GC state, and the drawing globals:
  `gfx_color`, `wm_clip_n`, `gfx_dis`, `gfx_b1ink`, `dsk_batch`, the display
  context (all reset in `gfx_unlock`, `vga12.inc:5149-5266`). It also means
  "the arrow may not be moved by the ISR", and "a batch of disk writes may
  stay open".
- **`sch_lock`, six meanings.**
  1. BIOS int 13h is not re-entered, and the mouse ISR may trust that no
     other task is mid-primitive (`mouse.inc:4240-4245`: "dsk_xfer holds
     [sch_lock] throughout, so no other task is running").
  2. An ES banked across a walk survives another task's shed: the FAT
     window, `disk.inc:6405`, `:6458`.
  3. The heap map is stable across a plan and its moves: `memory.inc:4040`.
  4. A PCM clip owns the CPU: `snd.inc:955`.
  5. The xmem driver copy is not interleaved, and on kern_mega a pool base
     does not move between read and use: `xmem.inc:640`, `mega.inc` passim.
  6. Hibernate freezes the machine's state while it writes the image:
     `hiber.inc:1843`.
- **IF=0.** It excludes ISRs. Its long holders are listed in ctx-4.

## Findings

### ctx-1 (rethink, HIGH, now): post instance teardown to the UI task, so paint and teardown have one context

**Evidence.** `inst_task_die` (`instance.inc:1921-1947`) runs on the dying
worker:

1. `gfx_lock`;
2. `inst_unwin` -> `wm_destroy` -> the damage pass, which far-calls every
   surviving package's `W_PAINT`;
3. `desk_drop_x` -> `mod_drop`;
4. `gfx_unlock`;
5. `mem_free_rec`;
6. `task_exit`.

Every one of these is either a cross-package call or a global write that the
rest of the kernel assumes happens on the UI task:

- histsys-1: a 128-byte slice carries a full repaint chain. `t_stkclass`
  "does not price the KERNEL below an `OSAPI_*` call"
  (`tests/unit/t_stkclass.py:31-36`), so no gate sees it.
- hm-3: DESK.DRV is dropped while the UI task may be inside `dkd_hover`
  (`ui.inc:1006`, no lock).
- ui-2: the UI reads `W_ONCLICK`/`W_ONRC`/`W_ONDRAG` before `gfx_lock`
  (`ui.inc:579`, `:1569`, `:1343-1356`), and the window it read can be
  destroyed by a worker that gets the lock first. 13.9.2 fixed this for the
  timer only (`ui.inc:823-831`).
- The worker's claim path cannot compact for that repaint, so raise caches
  decline a copy (`memory.inc:961-963`, which names `inst_task_die` as the
  caller).
- `mem_free_rec_x` stores `[mem_pg_floor] = MEM_LVL_TOP`
  (`memory.inc:4826`) from the worker. The UI task can be between the AVAIL
  that plans and the CLAIM that acts at a floor (`apps/dos/dos.asm:402-403`
  says the two must agree), and that store resets the floor.
- On kern_mega, `mega_fkill .worker` resets SP to the faulting frame and
  jumps into `inst_task_die` (`mega.inc:2686-2705`). The whole repaint chain
  then runs on the stack that just faulted.

**Model.** The UI task already drains six posted kinds of work at the top of
its pass, where nothing is held and `[wm_pkgd]` is 0 (`ui.inc:100-125`,
`:839-897`, `:897-900`). Teardown should be the seventh.

**Recommendation.** `inst_task_die` becomes the following:

```nasm
inst_task_die:                  ; DI = record, on the dying worker
    mov al, [sch_cur]           ; OWNER-DIED (ctx-2): a lock we hold is ours
    cmp al, [gfx_lock_own]      ; to give back (0xFF never matches a slot)
    jne .nolk
    call gfx_unlock
.nolk:
    call inst_idx               ; AX = index
    cli                         ; ...the post and the death in ONE IF=0 window
    mov byte [di+I_TASK], 0xFF  ; the record is task-less from here
    ; bts-equivalent on an 8086: set bit AX of [inst_reap]
    ...                         ; ~8 bytes (mov cl,al / mov ax,1 / shl ax,cl / or)
    inc byte [ui_post]
    xor bx, bx                  ; no release byte: the UI task frees the record
    jmp task_exit               ; (its own cli is a no-op now)
```

On the UI side, add one step to the posted ladder. For each bit of
`[inst_reap]`: `gfx_lock`, then `app_close_win`'s task-less tail from
`.noflush` (`inst_rel_rec`, `inst_unwin`, `mem_free_rec`, `desk_drop_x`,
`I_STATE <- 0`), then `gfx_unlock`. Factor that tail behind a label, which
is the same bytes. `toast_owner_gone` and `inst_rel_rec` move with it.

Correctness:

- The region is freed only after `task_exit`. The worker's frames reference
  its region but never return.
- `I_TASK <- 0xFF` is stored in the same IF=0 window that frees the slot. So
  no new spawn can inherit a slot that `I_TASK` still names. SPEC.md 20.6
  rule 2 names that hazard.
- The UI pass sees I_STATE = 2 and I_TASK = 0xFF. `app_close_win`'s `.hide`
  arm is idempotent for that state (`instance.inc:1662`).
- `mega_fkill .worker` keeps its `jmp inst_task_die` and gains the posted
  teardown for free.
- With rule 7 obeyed, no task but the UI task destroys or shows a window.
  ui-2's race then closes by construction, and `.tp_locked`'s re-test becomes
  belt and braces. Keep it: it is 5 bytes, and rule 7 is not enforced
  (ctx-7).

`INST_MAX` is 12 (`instance.inc:81`), so the mask is one `.bss` word.

**Bytes (ESTIMATED).**

| change | bytes |
|---|---|
| `inst_task_die` loses `gfx_lock`, the `I_WIN` load, `inst_unwin`, the far `desk_drop_x`, `gfx_unlock`, `mem_free_rec`, `mov bx, di` and `toast_owner_gone`/`inst_rel_rec` (about -28), and gains the owner check, the index, the bit and the post (about +20) | **~-8** |
| the UI drain: test, scan, bit clear, lock, call into the shared tail, unlock | **~+22 to +32** |
| the mask | **+2 `.bss`** |
| **net** | **~+16 to +26 resident** on kern_big |

That agrees with histsys-1's +15 to +30. On kern_small, desk_drop is absent,
so the figure is about 5 bytes less on both sides.

This also deletes the stated reason CWORD and RUNCPM must declare a
256-byte class (STACK-SLOTS-PLAN 12.4). It would let `t_stkclass`'s
1.25x bar stand without a kernel allowance.

**Gate.** Add a soak row that closes Bounce (class 128) over a second visible
window with `tools/stkwater.py` reading the slice. Break it on purpose by
reverting the post, and watch the high-water mark rise past the class.
histsys-1 named the row; this is its acceptance test.

### ctx-2 (bug, MEDIUM, now): a worker that holds the gfx lock when it dies deadlocks the desktop on kern_big and kern_small

**Evidence.** SPEC.md 20.6 rule 4: "Calling `OSAPI_TASK_ALIVE` while holding
it deadlocks, because `inst_task_die` takes the lock ... `gfx_lock` is a byte
with no owner field, so there is no kernel defence". The second half has been
false since `gfx_lock_own` (`vga12.inc:310-318`), and kern_mega already uses
it for exactly this. `mega_fkill .worker` reads it and calls `gfx_unlock`
before `inst_task_die` (`mega.inc:2694-2701`), with the comment
"inst_task_die takes the lock itself". kern_big and kern_small do not, so
`inst_task_die`'s `gfx_lock` (`instance.inc:1926`) spins forever on its own
hold. The scheduler keeps running and nothing ever draws again. The
self-take is also detectable in `gfx_lock`'s slow path generally, and it is
not detected.

**Recommendation.** Put the four-instruction owner check shown in ctx-1 at
the top of `inst_task_die`. Then delete kern_mega's copy in
`mega_fkill .worker`, which makes `.mega` about 13 bytes smaller. This is
OS/2 1.x's `ERROR_SEM_OWNER_DIED` reduced to its core: **a task's death
releases what it owns.** Optionally, in `gfx_lock .retry`'s *held* arm only
(off the fast path), add `mov al,[sch_cur] / cmp al,[gfx_lock_own] / je
<panel>`. It needs `push ax`/`pop ax`, about +10, and turns rule 4's silent
deadlock into a named panel through histsys-2's generalised panel.

**Bytes.** +9 `.text` (kern_big, kern_small); -13 `.mega`. The optional
self-take panel adds about +10 `.text`. If ctx-1 lands, the owner check is
already inside its figure.

### ctx-3 (rethink, MEDIUM, now): `sch_lock` can be released across the UI task's floppy int 13h, but only with three changes, and the first fixes a bug the release would create

**Evidence: the takers.** SPEC.md 7: "exactly **two** routines raise it".
The code has:

- `dsk_xfer` (`disk.inc:1462`), with its retry dance (`:2030-2035`) and exit
  (`:2433`);
- `dsk_dbg_raw` (`:888`);
- the FAT walks (`:6405`, `:6458`);
- the format path's `.i13` (`diskw.inc:6347`);
- the compactor (`memory.inc:4040`);
- `osapi_snd_play` (`snd.inc:955`);
- the xmem copy (`xmem.inc:640`, `:662`);
- hibernate (`hiber.inc:1843`, `:3039`);
- kern_mega's pool and copy sites (`fsx.inc:2244`, `megados.inc:302`, and
  about fifteen in `mega.inc`).

**What the release would break.** The mouse ISR's int 13h arm
(`mouse.inc:4226-4271`) has three cases:

- **Lock free:** drawing is safe *because* "dsk_xfer holds [sch_lock]
  throughout, so no other task is running to be in one".
- **Lock held:** it compares `[sch_cur]` against `[gfx_lock_own]`, "HELD BY
  US is the file-operation freeze".
- **Held by another task:** it refuses.

`[cur_inxfer]` is global, and `[sch_cur]` is read **at interrupt time**. With
switching live during the transfer, a worker can be scheduled, take the gfx
lock and be mid-fill when IRQ4 arrives. At that moment `cur_inxfer = 1`,
`sch_cur = worker = gfx_lock_own`, and the arm draws the arrow into the
worker's half-done primitive. That is FIELD-NOTES 34's permanent smear.
histsys-5 named `cur_inxfer` as a hazard but not this mechanism. Second, the
only thing that stops another task entering `dsk_xfer`, and with it the BIOS,
while the UI task is parked in the ROM is that nothing else can run. SPEC.md
20.6 rule 7 forbids it, and "None of this is enforced".

**What holds already.**

- **Heap:** on kern_big a worker gets first fit only, with no compaction and
  no shed (`memory.inc:955-965`). The compactor is the UI task's, and the UI
  task is in the transfer. So `memory.inc:3086`'s "it cannot land inside a
  floppy transfer" stays true. The destination is pinned (`mem_pinseg`).
- **DOS windows** install a refusing int 13h while their task runs
  (`apps/dos/dosw.inc:17`).
- **A ROM that hops stacks** is refused a switch by `sch_switch`'s SS test
  (`sched.inc:2408-2419`).
- **Hibernate, the compactor and the clip** each hold their own count, so a
  `dsk_xfer` nested inside them stays locked if the release is written as
  "do not take", not "drop".

**Recommendation.**

1. Make the ISR's int 13h arm task-scoped, as `CURBAR_ON` already is. Only
   the UI task calls `dsk_xfer` on this path, so the store can stay a
   constant. The ISR adds `cmp byte [sch_cur], 0 / jne .gates` after its
   `cur_inxfer` test: **+7 `.text`** at IRQ4, a few clocks. The test is not
   needed while switching is locked, but costs nothing to have.
2. Add a disk-owner byte to `dsk_xfer`, in `gfx_lock`'s shape: cli, test,
   take or yield-wait, sti. A rule-7 violator then waits instead of
   re-entering the ROM. **~+16 `.text`.** This is also the first runtime
   enforcement of rule 7's file half.
3. Skip `dsk_xfer`'s own `inc` only when all three hold: `[sch_cur] = 0`,
   `[sch_lock] = 0`, and the row is a BIOS floppy (`%ifndef KERN_MEGA`;
   `mega_fdx`/reflection keeps the lock). Mirror it at the two exits and the
   retry dance, whose `dec`/`inc` then pairs with nothing. **~+10 to +15.**

Measure before deciding, as histsys-5 says. Use a worker's frame count
during a 16 KB read on `os8088_5150_cga_gla`, `uilat`-shaped, because this
is the one number that decides the design. Also check the 8237 flip-flop
question in *Open questions* before a field run.

**Bytes.** **~+33 to +38 resident**, ESTIMATED. That is the upper end of
histsys-5's +20 to +40. The AT `int 15h` AH=90h hook adds another ~25 and is
optional.

### ctx-4 (hazard, MEDIUM, now): IF=0 has no bound and no instrument; set one per machine class and measure it in the final pass

**The long windows, from the code and the area reports.**

| window | where | length on the target | status |
|---|---|---|---|
| `mem_regrow` path 3 copies at IF=0 | `memory.inc:4559-4664` | 86 ms per 32 KB (the kernel's own figure, `memory.inc:2030`), ~170 ms per 64 KB | hm-1 |
| a driver image moves at IF=0 | `memory.inc:3072-3092` | 17 ms (SOUND) to 50 ms (ETHER), only when one moves | deliberate and documented |
| `mem_avail_lvl_p`'s walk | `memory.inc:4927-5023` | ~10-20 ms, PREDICTED | hm-5 |
| a kern_mega pool piece | `mx_cpmove` | ~16.4 ms per 64 KB piece | mc-5 |
| the arrow drawn inside `mou_isr` | `mouse.inc:4212-4300` | 2.4 ms CGA, 3.2 ms VGA | isr-7 |
| `mega_leds` spin in IRQ1 | `mega.inc:1311-1345` | up to 2 x 65,536 polls | mc-15 |
| `sch_switch` | `snd.inc`/`sched.inc` banner | ~2,200 cycles, 0.46 ms | inherent |

**What binds, per class.**

- **XT (4.77 MHz, 8250 UART):** the 8250 has a one-byte holding register,
  so the bound is the time from one byte becoming ready to the next byte
  completing. That is **~8.3 ms at 1,200 baud** (the serial mouse), ~4.2 ms
  at 2,400 (WireBall and the Serial link, per CLAUDE.md) and **~1 ms at
  9,600** (SERIAL.DRV's lease default, SPEC.md 115).
- **The clock:** the 8259 holds one IRQ0, so ticks are lost past 55 ms.
- **AT-class and kern_mega:** the same UART arithmetic, at 4-6x the CPU.

**Achievable.** An **8 ms bound on the XT is achievable** if three things
happen:

1. hm-1's copy moves under `sch_lock`, with one refinement: keep IF=0 for a
   block whose owner tag is the kernel's (`BH = 0xFF`: a driver's image or
   its claims, which an ISR of that driver can write). IF=0 does nothing
   against DMA, so `MC_DMA` claims are the caller's to quiesce either way.
   That is +~6 on hm-1's +10.
2. hm-5's walk goes under `sch_lock`.
3. The driver-image move stays a documented exception.

**A 1 ms bound is not achievable while the arrow is drawn in the serial
ISR.** That is isr-7's EOI-then-redraw rethink, +30 to +40. The honest
statement is "SERIAL.DRV at 9,600 on an 8250 shares the CPU with an
ISR-drawn arrow".

**Instrument.** Nothing in `tests/suite.py` measures IF=0 duration; I
searched it for IF=0, latency, overrun, `cli` and `serlease`. Proposal:

- **A knob kernel, `IFLAT=1`, in a private tree (0 shipped bytes).**
  `sch_isr`'s entry latches PIT channel 0 and keeps the maximum of
  (period minus count), which is how late IRQ0 was delivered. Its
  `sch_account` already latches and disambiguates the reload
  (`sched.inc:1243-1249`). Run the sub-tick at divisor 2048 (~1.7 ms) so
  every window longer than a period must contain an edge.
- **A soak row, `iflat`,** on `os8088_5150_cga_gla` and `os8088_xt_vga`. It
  drives a 32 KB Note Pad regrow, an `OSAPI_MEM_AVAIL` with 16 claims, and
  continuous mouse motion, then asserts the guest's own maximum against the
  class bound. MartyPC counts cycles, so host load cannot move the result.
- **Pair it with `serlease`** at divisor 12 counting LSR overrun bits, as
  isr-7 asks. `iflat` measures the cause and `serlease` the effect.
- **Break it on purpose** by reverting hm-1's change, and watch `iflat` go
  red at about 86 ms.

**Bytes.** 0 shipped. hm-1's fix becomes **~+16 `.cold`**, hm-5's +0 to +6.

### ctx-5 (hazard, MEDIUM-LOW, now): every ISR-callable door assumes SS = LOW_SEG, and the kernel itself says that is not always true

**Evidence.** `evq_push` writes the ring through SS: `[ss:di+EV_TYPE]` and
`push ss / pop es` (`events.inc:106`, `:117-118`). It justifies that with "SS
names [LOW_SEG] for every kernel task and every ISR on one ... and never
changed again". `sch_switch`'s own SS test contradicts it
(`sched.inc:2408-2419`): "a ROM that hops stacks for int 16h (SeaBIOS does,
for AH=01h, which ui_task polls with sch_lock CLEAR)". An IRQ that arrives
there lands on the ROM's SS. A DOS program's stack is another foreign SS, but
`dvw_irq0` moves IRQ0 only (`dosw.inc:22`).

`mou_isr` is safe because MOUPRIV loads SS. isr-2 is the case where
`osapi_mouse_feed` swaps SP and not SS. A package's own IRQ handler calling
`OSAPI_WM_WAKE`, whose cell says "Any context - ISR-safe" with no SS
precondition (`apps/os88api.inc:6605-6606`), writes a 6-byte record at
`foreignSS:evq_buf`. `kbm_isr`'s `sch_wake_ui` is DS-relative and is fine.

**Recommendation.** Have `evq_push` load ES from a constant: `mov ax,
LOW_SEG / mov es, ax`. AX is already banked. Move it above the full-ring
test so both accesses go through ES. **About +3 `.text`** on kern_big and
kern_small, a few clocks per event. kern_mega needs its selector spelling,
so verify LOW_SEG's PM value there. Then the "ISR-safe" claim is true
without a precondition. This is isr-2's family. The general rule for the
model: **an ISR-callable door may assume DS and nothing else.**

### ctx-6 (hazard, LOW-MEDIUM, now): on kern_small any task's claim may shed, which is the case the kern_big gate exists to forbid

**Evidence.** Both "ONLY THE UI TASK ... SHEDS" tests sit inside `%ifdef
OS88_COMPACT`: `mem_claim` (`memory.inc:954-965`) and `mem_regrow`'s
`.shed` (`:4703-4706`). The second gives its reason: "Fractal's worker grows
its cache here, and a shed from its stack could free a cache the pre-empted
UI task holds in a register". kern_small has no compactor, but it does have
purgeable claims and workers (`MAX_TASKS` 7), so the reason applies there
unchanged. The FAT walk defends itself with `sch_lock` because of exactly
this (`disk.inc:6405-6420`: "a purgeable one is shed by any ordinary claim
from any other task"). Whether every other UI-side holder of a purgeable
claim on kern_small does the same is not stated anywhere. PLAUSIBLE, not
reproduced.

**Recommendation.** Take the two `cmp byte [sch_cur], 0 / jne` pairs out of
the `OS88_COMPACT` guard: **about +14 `.text` on kern_small**, 0 on kern_big.
Or state the kern_small rule in SPEC.md 66.3.1 and list the holders that
defend themselves. The first is cheaper than the audit.

### ctx-7 (weakness, MEDIUM, now): the context rules should be tags and a gate, not two prose lists

**Evidence.** The two "what a worker may call" lists differ:

| | names |
|---|---|
| SPEC.md 20.6 rule 7 only | `wm_saveu`; `OSAPI_DRV_CALL`, in the paragraph after the list |
| `apps/os88api.inc:3969-3974` only | `OSAPI_GFX_PEN`; `OSAPI_WM_CLIP_TEST` |

Each list has been amended twice for "missing ... reads as forbidden by
omission" (SPEC.md 20.6 rule 7's own paragraphs). Rule 7 ends "None of this
is enforced". The kernel side has the same gap one layer down. `t_stkclass`
stops at the far call (`tests/unit/t_stkclass.py:31-36`). Nothing walks
*kernel* routines from a worker entry, which is why `inst_task_die`'s
repaint on the worker was invisible.

**Recommendation.** Do abi-8, with two additions:

1. **The vocabulary is the matrix's columns:** `@ctx ui`, `ui-locked`,
   `any`, `worker`, `isr`, `hook`, `bracket`, plus `@blocks` (may yield or
   wait) and `@moves` (may compact). `@moves` is the Mac's "may move
   memory" table. Generate both lists from the tags: docs/INDEX.md, and
   isr-16's "callable at interrupt time" table.
2. **Extend the gate across the boundary.** Tag the handful of kernel entry
   points a worker can reach (`inst_pkg_alive`, the drawing slots,
   `wm_wake`). Have `tools/stkdepth.py`'s kernel graph assert two things:
   - no `@ctx worker` root reaches a routine that calls `wm_pkgcall` or
     `mod_drop`;
   - no `inc byte [sch_lock]` ... `dec` span reaches a `@blocks` routine.

   The second check is ctx-8's lock order, gated.

**Bytes.** 0 shipped. The tags live in comments that `abilock` already
fingerprints.

### ctx-8 (weakness, LOW, now): the lock order "never block under `sch_lock`" is obeyed and unwritten

**Evidence.**

- `task_yield` with `[sch_lock]` raised returns to the caller at once
  (`sched.inc:1881-1883`). So `task_sleep` under the lock stores `T_STATE =
  2` and returns, and the running task carries a sleeping state until its
  deadline. A contended `gfx_lock` under the lock spins for ever, because the
  holder is never scheduled.
- Every site that must wait drops the lock round the wait by hand:
  `memory.inc:4101-4103` (the park) and `disk.inc:2030-2035` (the busy
  chrome).
- `wm.inc:3263` is the only statement of the rule.
- `mega_fkill` treats a package fault under `sch_lock` as fatal
  (`mega.inc:2564-2565`). Every widened hold therefore also widens kern_mega's
  fatal window.

**Recommendation.** Write it into SPEC.md 7 as a numbered rule. Then put the
tripwire in `gfx_lock .block`, which only runs when the lock is held: `cmp
byte [sch_lock], 0 / jne <panel>`. About +7 `.text`, off the fast path. Or
gate the order statically under ctx-7 for 0 bytes.

### ctx-9 (rethink, LOW, flagday or opt-in): deferred damage, reconciled

**Where it stands after ctx-1.** Rule 7 forbids workers every window-changing
slot, and ctx-1 moves the last worker-side destroy. So every damage source
is on the UI task, and the question histsys-10 and wm-R2 ask is no longer
about safety. Two things remain:

- **Coalescing.** A task-owned close is a hide and then a destroy, which is
  two passes. So are several `OSAPI_WM_RESIZE` calls in one handler.
- **Nesting depth.** A paint runs inside another package's callback on the
  UI task's 1,024 bytes. `WM_PKGD_MAX` is 8 and "three deeper than anything
  measured" (`wm.inc:170-175`).

**Reconciling the two estimates.**

- histsys-10's +60 to +120 is a global accumulator and a drain, applied to
  every operation, at a flag day.
- wm-R2's +60 to +100 is a per-window owed rect, opt-in. A per-window rect
  in the record would cost 8 x `MAX_WIN` = **96 `.bss`** on kern_big alone.
  The cheaper form is a **global four-rect list (32 `.bss`)** fed through the
  `wm_clip_tab` splitter histsys-10 names, plus about 50-70 of code for
  "post", "drain at the pass top" and the opt-in test.
- A single bounding union is ~8 `.bss` + ~40, but it repaints more than
  changed (PERFORMANCE.md rule 1), so it is not a candidate.

So the two estimates are the same design at **~+80 to +100 resident**. The
low ends of both assumed a structure smaller than the one rule 1 permits.
Record it, and decide it with wm-R2's plan. OS/2's `CS_SYNCPAINT` shows the
opt-in shape. Default synchronous, per-window deferred.

## Rethinks

- **R1, a UI work queue instead of seven post bytes.** The posted work is
  `inst_launch`, `wm_clreq`, `wm_tarm`, `mega_fkq`, `mega_tellq`,
  `ui_rebootq`, the posted compaction, and ctx-1's `inst_reap`. Each has its
  own byte or mask and its own rung in `ui_task`'s ladder, summarised by
  `[ui_post]`. Win 3.x's answer was one queue of (hwnd, msg, wParam)
  records; this kernel already has one in `evq` (16 records). Teardown,
  close and launch as `EVT_*` records would retire several bytes and their
  ladder rungs. But the ring is lossy by design (SPEC.md 10.1 drops the
  oldest non-WAKE). A teardown must not be dropped, so it would need
  `EVT_WAKE`'s exemption. Not recommended now. The masks are cheaper than
  making the ring lossless. Record it for whoever adds the eighth.
- **R2, `sch_lock` split by meaning.** OS/2 1.x had no global preempt-off
  for the disk path. A thread blocked on the device's semaphore and others
  ran. After ctx-3 the int 13h meaning has its own byte. The heap meaning
  (`memory.inc:4040`) and the FAT-window meaning are already redundant on
  kern_big, because only the UI task compacts or sheds. They are real on
  kern_small (ctx-6). The clip, hibernate and kern_mega meanings are
  genuinely "the CPU is mine". A full split costs a mutex byte per meaning
  and buys little beyond ctx-3. Record it.

## Prior art, mapped to this kernel's bytes

- **OS/2 1.x RAM semaphores** (`DosSemRequest`/`DosSemClear` on a 4-byte
  RAM semaphore; system semaphores with an owner and `ERROR_SEM_OWNER_DIED`).
  `gfx_lock_flag`/`own`/`want`/`last` *is* a 4-byte RAM semaphore
  (`vga12.inc:296-323`), with fairness OS/2's lacked. What OS/2 had and we
  lack is owner-died release: ctx-2, +9 resident, -13 `.mega`. The disk
  semaphore that let OS/2 run other threads through a floppy read is ctx-3's
  owner byte, +16. OS/2's per-thread message queues (`WinCreateMsgQueue`)
  are not worth their bytes here: 12 tasks times even an 8-record queue is
  most of 1 KB. The per-window `W_WKQ` byte (`wm.inc:2336-2338`) is the
  one-message queue that buys most of it, so record it.
- **Win 3.1's interrupt-time rule.** In practice an interrupt handler may
  call `PostMessage` and touch FIXED, page-locked memory. Here
  `PostMessage` is `OSAPI_WM_WAKE`, already one per window and coalesced:
  - Windows 3.1's validation at the door is histsys-11's `wm_isrec`, +6 to
    +12. It is a range check only, because `WIN_SIZE` is 74 and not a power
    of two (`wm.inc:127`). An alignment check would need a `div` at
    interrupt time, so range is the right 80/20.
  - FIXED is our `MC_RLOC` = 0 (PINNED by default, §66). The IVT rewrite in
    `mem_rr_ivt` (`memory.inc:3167`) goes further than Windows did: a moved
    driver's vectors follow it.
  - What Windows 3.1 wrote down and we have not is the *list*. That is
    ctx-7's `@ctx isr` tags, 0 bytes. The precondition Windows got for free
    (the interrupt stack was the VMM's) is ctx-5's SS fix, +3.
- **The Mac's update events and "may move memory".** The Window Manager
  accumulated `updateRgn` per window, and the Event Manager synthesised
  `updateEvt` only when the queue was otherwise empty. No application's paint
  ever ran inside another's handler. The byte-honest version here is ctx-9's
  global four-rect list at ~+80 to +100, opt-in. Inside Macintosh's "routines
  that may move or purge memory" appendix is ctx-7's `@moves` tag. In this
  kernel the fact is already partly enforced in code, because only the UI
  task compacts. A worker therefore never sees a move during its own call,
  and the tag is for the UI task's callbacks across `mem_claim`, 0 bytes.
- **Mac interrupt safety (Inside Macintosh's table of routines callable at
  interrupt level).** It maps to the same `@ctx isr` column. It is also the
  model for the `H` (rate hook) row: "no kernel calls, 48 bytes" is §53.2.2's
  equivalent of the Mac's "no Memory Manager calls at interrupt time".

## Doc/code mismatches

1. **SPEC.md 7, "Scheduler lock"** says: "Outside the scheduler's own
   internals, exactly **two** routines raise it ... `disk_read` across its
   int 13h window (§18), and the clip door `OSAPI_SND_PLAY`". The code has
   about forty takers (ctx-3's list: `disk.inc` 888/1462/6405/6458,
   `diskw.inc:6347`, `memory.inc:4040`, `xmem.inc:640/662`,
   `hiber.inc:1843/3039`, `snd.inc:955`, and kern_mega's `fsx.inc:2244`,
   `megados.inc:302` and `mega.inc`). Also, `disk_read` is not the routine;
   `dsk_xfer` is. *Proposed:* "Outside the scheduler's own internals it is
   raised by three groups, all with the same meaning - the tick still runs,
   the BIOS chain feeds the floppy motor, `sch_account` runs and sleepers
   mark ready; only involuntary switching pauses. (1) Every BIOS disk call
   (`dsk_xfer` across its `int 13h`, §18; the format path and the debug
   door), and the FAT walks that bank the window's segment (§18.8.4). (2) A
   pass that must see the heap or a kern_mega pool unchanged between a plan
   and its moves (the compactor, §66.4; the xmem copy, §41; kern_mega's pool
   sites). (3) The two that own the CPU outright: the clip door
   `OSAPI_SND_PLAY` (§34.4) and hibernate's image write (§87). Nothing may
   block while it is raised: a yield under it returns at once (§8)."
2. **SPEC.md 7, first bullet** says: "`gfx_lock`: `cli`; if flag set → `sti`,
   `call task_yield`, retry; else set flag, `sti`, then `call cursor_hide`."
   The code (`vga12.inc:5082-5136`) stamps the owner and the last taker in
   the same word store, applies 7.3's waiter debt, and *promises* the hide
   (`mov byte [cur_lazy], 1`, 7.1.4) instead of calling `cursor_hide`.
   *Proposed:* "...else set the flag and its owner (`[gfx_lock_own]` =
   `[sch_cur]`) in one store, `sti`, and promise the hide (§7.1.4: the arrow
   is taken down only when something is about to draw over it). §7.3 adds
   the one-debt fairness rule."
3. **SPEC.md 20.6 rule 4** says: "`gfx_lock` is a byte with no owner field,
   so there is no kernel defence". `gfx_lock_own` exists (`vga12.inc:310`),
   and kern_mega uses it for this very defence (`mega.inc:2694-2701`).
   *Proposed:* "`gfx_lock` records its owner (`[gfx_lock_own]`, §59.4), but
   only kern_mega's fault path reads it here - on kern_big and kern_small
   there is no kernel defence yet."
4. **SPEC.md 20.6, the `0x0158` row** says: "...`task_exit` with BX = the
   record, which frees the record and with it the package region inside one
   IF=0 window". `inst_task_die` frees the region with `mem_free_rec` at
   IF=1 after `gfx_unlock`, before `task_exit` (`instance.inc:1945-1947`).
   `task_exit`'s IF=0 window frees only the record and the slot.
   *Proposed:* "...`gfx_unlock`, `mem_free_rec` (every heap claim, region
   included, §50.4), then `task_exit` with BX = the record, which frees the
   record and the task slot inside one IF=0 window."
5. **SPEC.md 29.2 rules 2 and 7** say: "a worker-owning package's region is
   released from `task_exit` under IF=0" and "There is no explicit
   region-free call — the store that frees the record frees the region". The
   region is a heap claim freed explicitly by `mem_free_rec`, on both paths
   (`instance.inc:1736`, `:1945`; `memory.inc:4820-4826`). Rule 7 describes
   format v2's derived occupancy, before §50. *Proposed for rule 7:* "Since
   §50 the region is an ordinary heap claim of the instance and is freed
   explicitly by `mem_free_rec`, on both free points, before the record is
   released; occupancy is no longer derived from `I_STATE`." Rule 2's tail
   needs the matching change.
6. **`kernel/vga12.inc:299-305`** says: "Four writers in the tree and no
   fifth: :5303 (1), :5384 (0), sched.inc :1683 (0) and :1699". The writers
   are now at `vga12.inc:5124` (take) and `:5262` (release), both word
   stores, at `sched.inc:2098` and `:2114` (KFZTRACE only), and at
   `mega.inc:2491` (the fatal panel), `:7569` and `:7582`. *Proposed:*
   "Writers: gfx_lock's take and gfx_unlock's release (both one word store
   with the owner), and the forced-open diagnostics - sched.inc's KFZTRACE
   strip (banked) and kern_mega's fault panel and its own strip in mega.inc.
   No line numbers: grep `[gfx_lock_flag]`."
7. **`kernel/toast.inc:398-404`** says: "gfx_lock writes flag then owner
   inside its own cli window and gfx_unlock writes owner FIRST then flag, so
   the only interleaving visible is (1, 0xFF)". Both are single word stores
   now (`vga12.inc:5124`, `:5262`; the comment at `:5263-5267` says so).
   *Proposed:* "Torn reads cannot happen at all: gfx_lock and gfx_unlock
   each write the flag and the owner in ONE word store, and an 8086 takes
   interrupts only at instruction boundaries."
8. **`apps/os88api.inc:3966`** says: "Your stack is SCH_STACK (384) bytes in
   LOW_SEG". The worker's stack is the class the header declares: 128, 192,
   256 or 384 (SPEC.md 20.6 rule 6, `sched.inc:895-897`). *Proposed:* "Your
   stack is the class your header declares (OS88_STACK_128/192/256, else
   SCH_STACK = 384), in LOW_SEG, and SS != DS".
9. **`apps/os88api.inc:3996`** says: "The kernel's own app_bounce_task is the
   reference implementation of all of this." It is gone (`kernel.asm:8251`:
   "app_tmr_kinit, app_bounce_kinit and app_bounce_task stood here").
   *Proposed:* "apps/bounce/bounce.asm's bn_worker (SPEC.md 112.2) is the
   reference implementation of all of this."
10. **`apps/os88api.inc:6605-6606`** (`OSAPI_WM_WAKE`) says: "Any context -
    ISR-safe and worker-safe, no lock needed." The ring is written through
    SS (`events.inc:106`, `:117`), so an ISR must arrive on a LOW_SEG stack.
    *Proposed, until ctx-5 lands:* "Any context with SS = LOW_SEG (every
    task, and an ISR that interrupted one) - worker-safe, no lock needed."
11. **`kernel/events.inc:106-107`** says: "the ring is in LOW_SEG now ...
    which SS names for every kernel task and every ISR on one".
    `sched.inc:2413-2419` records SeaBIOS hopping stacks for int 16h with
    `sch_lock` clear, and an IRQ there arrives on the ROM's SS. *Proposed:*
    "...which SS names for every kernel task and for an ISR that interrupted
    one - NOT for one that landed on a ROM's own stack (sch_switch's SS test,
    SPEC.md 8.5), which is why mou_isr loads its own SS."
12. **`kernel/memory.inc:4043-4045`** says: "The plan is pure arithmetic over
    32 records and costs microseconds". On a 4.77 MHz 8088, `mem_cp_plan`
    walks `mem_cp_next`, which scans all 32 records per live one
    (`memory.inc:2680-2720`), plus the per-record predicates. hm-5 predicts
    ~10 ms. *Proposed:* "The plan is arithmetic over 32 records - O(n²) in
    mem_cp_next, milliseconds on the 8088 (hm-5 predicts ~10 ms for 16 live
    claims; unmeasured) - and taking it inside the same window...".
    PLAUSIBLE: the order of magnitude is certain, the figure is predicted.
13. **`kernel/disk.inc:6407-6410`** says: "a purgeable one is shed by any
    ordinary claim from any other task". That is true on kern_small only. On
    kern_big only the UI task sheds (`memory.inc:955`, `:4703`).
    *Proposed:* "...shed by any ordinary claim from any other task on
    kern_small, and by the UI task's own claims on kern_big (SPEC.md
    66.3.1)."
14. **`kernel/wm.inc:3235-3236`** says: "wm_lk / wm_unlk - THE DRAWING MUTEX,
    FOR THE THREE ENTRIES A PACKAGE REACHES WITH IT FREE", and at `:3263`
    "all three run on the UI task". There are four doors: `osapi_wm_resize`
    joined (`:3313-3317`; `wm_lk_go` says "the four doors' one body",
    `:3334`). `wm_unlk` is no longer a routine (`:3290-3291`). *Proposed:*
    "wm_lk - THE DRAWING MUTEX, FOR THE FOUR ENTRIES A PACKAGE REACHES WITH
    IT FREE (show, hide, front, resize; SPEC.md 11.101.2, 11.1.2)" and "all
    four run on the UI task".

## Open questions

- **The 8237 flip-flop under ctx-3.** IBM's XT BIOS sets up DMA channel 2
  under `cli`. A clone BIOS that does not, pre-empted between the two
  address-byte writes by a SOUND.DRV refill task programming channel 1
  (SPEC.md 34.5 makes those tasks), would get a mis-sequenced flip-flop.
  Which BIOSes in `vm/` and FIELD-MACHINES.md set up DMA with IF=1? This
  needs a disassembly read before ctx-3's field run.
- **Does any shipped package call `OSAPI_WM_WAKE` from its own IRQ
  handler** (ctx-5)? The grep finds callers in WireChat, the browser, The
  Wire, DOS and others, but none was traced to IRQ context. If none does,
  ctx-5 is latent and the doc fix (mismatch 10) suffices for now.
- **On kern_small, which UI-side holders of a purgeable claim run with IF=1
  and switching live** (ctx-6), besides the FAT walk that defends itself?
  The raise cache and the directory read-ahead are the candidates.
- **ctx-1's ordering against `.tp_locked`.** After the post, can a
  timer-due window be destroyed between the UI's read and its lock? Only by
  a rule-7 violator, or by kern_mega's `mega_fkq`, which drains on the same
  task. `timerrace` should be run once in the final pass to confirm the
  re-test is now redundant, not to remove it.
- **kern_mega's reflected int 13h.** ctx-3 keeps `sch_lock` there. Whether a
  native PM floppy path (`mega_fd13`) could ever run with switching live
  belongs to megacore, not here.
