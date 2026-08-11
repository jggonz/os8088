# Converting `xmem.inc` into a driver — an investigation

**Research document. Nothing here is built.** SPEC.md is the binding contract
for what the kernel *is*; this is the study of moving the store above 1MB
(SPEC.md §41) out of the kernel image and into a loadable `.DRV` (SPEC.md
§51), and the record of what was measured while asking.

The ask, in the requester's words: *investigate converting `xmem.inc` into a
driver.*

---

## 0. The verdict, up front

**It converts cleanly — every mechanical objection has an answer already in
the tree — and it should not be done yet, because the thing it produces is a
driver nobody would ever load.**

The prize is real and was measured rather than argued. Removing `xmem.inc`
from `kern_big` and leaving the tier-0 stubs the `%else` branch already
carries:

```
kernsize[big]: sections   text 58,338 -1,034  bss 5,580 -124  ovl 2,752 -386   (sum -1,544)
kernsize[big]: rungs      image 64,000 -1,536 (82 left, was 460)
kernsize[big]: footprint  KERN_SIZE 101,376 of 104,960 -> 3,584 spare (7 steps), was 2,048
kernsize[big]: segment    .text+.bss 63,918 of KERN_CODE_MAX 65,536 -> 1,618 left
kernsize[big]: ladder     HEAP 0x1920 = 100.5 KB   (was 0x1980 = 102.0 KB)
kernsize[big]: *** the image rung CROSSED: 128 -> 125 steps of 512 ***
```

Read the **segment** line first. `.text` + `.bss` must fit the kernel's own
64KB window and `KERN_CODE_MAX` is the one guard **no conversation can
raise** — 16-bit offsets. It stands at **460 bytes left**. This removal is
worth 1,158 of them, taking it to 1,618: the difference between "the next
feature has to ask" and "the next three do not". The footprint guard moves
too (2,048 → 3,584 spare, three 512-byte rungs), and every machine gets
**1.5KB more heap** because the ladder shortens.

Three findings sit against that:

1. **Nothing consumes the pool, so nothing would tick the box.** SPEC.md
   §51.3 is binding — *nothing loads that `SYSTEM.CFG` did not ask for* — and
   the module's own header already says no code in the tree allocates from
   this store on any tier. A driver off by default whose only consumer does
   not exist is a feature that answers "no store" on every machine in the
   world, including the 386 with 8MB where the slots answer honestly today.
   **That is functionally a deletion with a floppy file attached.**

2. **The `drv_snd_sniff` escape does not work here** (SPEC.md §51.3.1). The
   sound row defaults its `DRVR_WANT` from a ~2 ms OPL2 timer probe — cheap
   because the probe and the driver are utterly different sizes. The
   analogous question here is *is there memory above 1MB*, and answering it
   **is** the A20 gate plus `int 15h` AH=88h — which is precisely the boot
   cost SPEC.md §41.11 was pleased to take off `kern_small`. You would put
   the boot probes back to auto-enable a feature nothing uses.

3. **Everything else is a cost, not a wall.** A new class, a third dispatcher
   body, a `DSV_XM` sub-table, a rewritten gate, and one more file on the
   system disk — §4 prices them. None is hard. All of it is spent on the two
   findings above.

**Recommendation: not now, and the trigger for revisiting is a CONSUMER, not
a byte count.** If the byte pressure is the actual problem, §6 has two
cheaper answers that do not invent a driver class. If a consumer appears —
something that genuinely wants bulk storage above 1MB — then a driver is the
*right* home for it, because at that point the user has a reason to tick the
box and the app has a reason to tell them to.

---

## 1. What is actually in the file

`kernel/xmem.inc` is 1,501 lines and four separable things. The split matters,
because a conversion does not have to move all four:

| part | routines | section | what it is |
|---|---|---|---|
| **prerequisites** | `xm_a20_probe`/`_settle`, `xm_kbc_wait`, `xm_fast_a20`, `xm_kbc_a20`, `xm_a20_enable`, `xm_hma_claim` | `.ovl` | open and **verify** the A20 line; decide whether the HMA is claimed and therefore where the pool starts |
| **sizing** | `xm_init` | `.ovl` | `int 15h` AH=88h, clamp for tier 1's 24-bit descriptors, zero the block table, arm unreal mode, publish `[xm_kb]` last |
| **the allocator** | `xm_caps`, `xm_alloc`, `xm_free`, `xm_chk`, `xm_owner`, `xm_release_inst`, `xm_release_rec` | `.text` | 8 entries, 1KB granularity, owner-stamped, force-freed at instance teardown |
| **the transports** | `xm_copy`, `xm_arm`, `xm_ucopy`, `xm_bios` | `.text` | one ABI over two transports — unreal mode through FS/GS on tier 2, `int 15h` AH=87h on tier 1 |

Whole-module cost today, from `kernsize.py --modules`: **1,040 bytes of
`.text` and 124 of `.bss`**, plus the `.ovl` half, which the removal
measurement puts at **386**.

Above it sit four published slots — `OSAPI_XMEM_CAPS` / `_ALLOC` / `_FREE` /
`_COPY` at 0x0190..0x01A8 — and exactly five kernel-side readers:

- `kernel/memory.inc`, one `mov ax, [xm_kb]` filling `SK_XMS` for the Task
  Manager's XMS line;
- `kernel/instance.inc`, three `call xm_release_rec` at SPEC.md §29.4's
  teardown sites;
- `kernel/loader.inc`, one more through the cold shim `cw_xm_release_rec`;
- `kmain`, two overlay calls (`ovl_xm_a20`, `ovl_xm_init`);
- `tests/sysbench` and `tests/xmtest`, from outside.

That is the whole blast radius, and it is small. **The feature is already
almost detached** — SPEC.md §41.11 detached it from `kern_small` a round ago
and `xmem.inc` has one `%ifdef` around the entire body.

---

## 2. The precedent is exact, and it is the sound layer

This is not a new shape. `snd.inc` is the worked example of the same
conversion already done: **the kernel kept the ABI and the policy, the
hardware went to a driver.** `osapi_snd_fm` (slot 0x00F8) stays resident,
stamps the requesting instance into DH out of kernel state the driver cannot
see, and far-calls `DSV_FM` through `drv_svc_call`. The card code — the OPL2
probe, the DSP reset, the DMA ring — is `SOUND.DRV`, and a machine with no
card carries a `drv_tab` row and a file it never reads.

Map that onto §1's table and the seam draws itself:

| stays in the kernel | goes to `XMEM.DRV` |
|---|---|
| the four cells at 0x0190..0x01A8 | their bodies |
| stamping *who is asking* (`xm_owner` → `snd_req_inst`) | the allocator and its table |
| raising `[sch_lock]` around the transport (§3.2) | both transports, the GDT, the unreal-mode arm |
| `SK_XMS`, via a dispatch | A20, the HMA claim, `int 15h` AH=88h |
| `xm_release_rec`, as a `DSV_RELINST` dispatch | the force-free walk behind it |

**The two things that must NOT move** are the slot numbers and the register
contracts (SPEC.md §20.8 rule 4 — the table is unfrozen in alpha, but a
re-contracted cell at an old number is the failure that assembles cleanly and
runs wrong). A package would see no difference at all, which is the property
that makes the conversion legitimate.

---

## 3. What was checked, and what it answered

### 3.1 Boot ordering is NOT a blocker — this was the expected wall and it is not one

`kmain` calls `ovl_xm_a20` and `ovl_xm_init` **before `sched_init`**, and its
comment says why: *"this is the last moment at which no kernel ISR is
installed — the unreal-mode window inside `xm_init` runs with CR0.PE set and
a real-mode IVT, so the only handlers that may fire in it are the BIOS's
own"*. A driver loads at `drv_boot`, which is after `sched_init`, after
`mem_init` and one call before the first `wm_paint_all`. On the face of it
that kills the conversion.

**It does not, and the file itself is the proof.** `xm_arm` masks NMI at port
0x70 and runs the entire PE window inside one `pushf`/`cli` … `popf`, and it
is **already called at run time from `xm_ucopy`**, once per 1KB chunk, with
`int 08h` hooked, IRQ3/IRQ4 live and the mouse UART running. If a kernel ISR
could break the transition, the shipped transport would have been broken
since it was written. The early call is a *comfort* — one less thing true in
the window — not a constraint. `drv_boot` is a legal home.

Two neighbours were checked with it:

- **The A20 probe's scratch stays free.** `xm_a20_probe` writes one word at
  linear `0x0500` and one through the alias at `HMA_SEG:0510`, saving and
  restoring both under `cli`. `KERNEL_SEG` is 0x0060, so the kernel starts at
  linear 0x600, and SPEC.md §18.92's diskette parameter table sits at
  `0000:0580`. The probe touches `0x500..0x501` and nothing else. Still free
  at `drv_boot`.
- **The GDT works from a heap segment.** `xm_arm` computes the `lgdt` base
  from DS at run time rather than baking `KERNEL_SEG` in, and a driver image
  is an ordinary heap claim well under 1MB — so the 24 base bits a 16-bit
  `lgdt` loads are still exact.

### 3.2 `[sch_lock]` has no API slot — and the kernel is where it belongs anyway

Both transports raise the scheduler lock: `xm_ucopy` across the whole chunked
copy, `xm_bios` around `int 15h` AH=87h. A driver cannot reach it. This is
not a guess — `drivers/debug/debug.asm` records it in its own header as the
reason that driver has no `call` verb and no disk payload channel: *"and
`[sch_lock]` has no API slot. Adding one is kernel code; until somebody asks
for it…"*.

**The resolution is already in the tree and needs no new slot.** `dsk_xfer`
raises `[sch_lock]` *before* dispatching `DSV_BLK`, so a block driver is
handed a locked scheduler rather than taking one — `drivers/net/net.asm` says
so twice. The same shape works here: the kernel's `xm_copy` cell raises the
lock, far-calls the driver, and drops it. The driver keeps its own per-chunk
`cli` window, which is a different mechanism protecting against a different
thing (an ISR, not a task switch) and needs nothing from the kernel.

The consequence worth stating: **`xm_copy`'s context rule becomes the
kernel's to enforce, not the driver's to document.** That is an improvement —
SPEC.md §41.8's *UI task only, gfx lock may be held, never from a worker* is
a rule about kernel state, and it currently lives in a comment above the
routine that depends on it.

### 3.3 `DSV_*` is full, so this needs a sub-table

`DSV_SIZE` is 28 — fourteen words, all spoken for. `DSV_FS` (SPEC.md §62.9.1)
is the precedent and its reasoning applies unchanged: the service table is
copied **per class** into `.bss`, so a cell costs `2 × DRVC_MAX` bytes on
every machine including the ones with no such driver. So a memory driver
publishes one `DSV_XM` pointer to an `XMV_*` table in its own segment —
`XMV_CAPS`, `XMV_ALLOC`, `XMV_FREE`, `XMV_COPY` — and reuses the existing
`DSV_RELINST` for teardown, which is already generic (*"AL = an instance slot
that is being torn down"*) and already per class.

### 3.4 A third dispatcher body

`drv_svc_call` names `drv_fptr`/`drv_fseg` outright — **the sound class** —
because SPEC.md §51.2.1's register argument leaves it nothing to carry a
class in. `drv_blk_call` is the second body and takes its class from
`[drv_blkcls]`, a `.bss` byte. `drv_cp_call` is a third shape and takes the
class in AL, because the Control Panel page ABI leaves AX alone.

The XM ABI spends ES:SI, DX:AX, CX and DI on `xm_copy` alone, so AL is not
free either: it wants `drv_blk_call`'s memory cell, or its own hardcoded
body. **Twenty bytes either way** — the same trade §51.2.1 already took.

### 3.5 Four things that need no new mechanism at all

- **`xm_owner` needs no slot.** `osapi_snd_fm`'s header states the rule: *the
  requesting instance is stamped in the kernel, not in the driver, because
  `snd_req_inst` reads kernel state and a driver that had to ask would need
  an API slot of its own.* The kernel's `xm_alloc`/`_free`/`_copy` cells stamp
  DH and pass it, exactly as the FM slot does.
- **`xm_release_rec` fits `DSV_RELINST`.** `snd_release_inst` already
  dispatches that cell from the dying task at SPEC.md §29.4's teardown sites,
  so the context is precedented rather than novel. `instance.inc` keeps
  calling one kernel routine and never learns that anything moved — which is
  the same argument SPEC.md §41.11 made for keeping a `ret` in `kern_small`
  rather than three `%ifdef`s in a file that is not about extended memory.
- **`[cpu_feat]` needs no slot.** SPEC.md §41.11 established by grep that
  `CPU_F_A20` / `_HMA` / `_UNREAL` have **no readers** outside `cpudet.inc`
  and `xmem.inc` — not in the kernel, not in a package, not in a driver, not
  in the benchmarks. So the bits simply stop being written when the driver is
  absent and `cpu_info`'s AH answers 0, which is *exactly* what `kern_small`
  answers today and is the truth on any machine with no gate verified.
- **The floppy has room.** The 360KB system disk is at 149 of 354 clusters,
  and an `XMEM.DRV` of ~1.6KB is four of them.

### 3.6 Three things that would be genuinely new obligations

- **A20 outlives detach.** `DRVV_DETACH` cannot fail and is documented as
  *restore the ports you saved at attach*. The A20 line is machine state, not
  a hooked resource: nothing in os8088 depends on 1MB wraparound, so leaving
  it open is safe and re-closing it buys nothing. That needs saying out loud
  in the spec, because the alternative reading — a driver that closes A20 on
  its way out — is a machine whose HMA aliases change under a BIOS that may
  have opinions.
- **Unreal mode outlives detach too.** FS and GS keep their 4GB limits after
  the image is freed. Harmless in itself (a wider limit faults nothing), but
  SPEC.md §41.4's rule — *only `xmem.inc` writes FS or GS* — becomes *only
  `XMEM.DRV` writes them*, and it must now hold across a window in which the
  driver **is not loaded** and the limits are still wide. Still checkable by
  the same case-insensitive grep; worth restating rather than inheriting.
- **Detach must force-free the whole block table.** `DRVV_DETACH` cannot fail
  and every outstanding block's owner is about to lose its allocator. That is
  `xm_release_inst` over every slot, and it is four lines — but it is a leg
  that does not exist today because the table only ever dies with the kernel.

### 3.7 The gate breaks, and it is the only one there is

`tests/xmcheck.py` reads `xm_tab` out of the running guest by kernel symbol
(`tools/os88sym.py`) and diffs it across a package's teardown. It is the only
thing that exercises the allocator and the force-free at all — the module's
own header says so, and it exists because those three `call xm_release_rec`
sites were **silently absent for a year** after an integration merge dropped
them.

Moved into a driver, `xm_tab` is at a heap segment known only at run time.
The gate is rewritable — read `drv_tab` row N's `DRVR_SEG` from the kernel's
map, then read the table at that segment plus a fixed offset from the
driver's own map — but it is real work on the one instrument standing between
this feature and the exact failure it has already suffered once. **Budget for
it as part of the conversion, not after it.**

---

## 4. What the conversion would cost

Measured where it could be measured, hand-counted where it could not, and
labelled either way.

**Removed from the kernel** (measured, §0): `.text` −1,034, `.bss` −124,
`.ovl` −386.

**Added back** (estimated):

| item | `.text` | `.bss` |
|---|---|---|
| four slot bodies: stamp DH, raise `[sch_lock]`, dispatch | ~150 | — |
| a third dispatcher body (§3.4) | ~20 | 1 |
| `DRVC_MEM` = 6: `drv_fptr6`/`drv_fseg6`, `DRVC_MAX` 5 → 6 | 4 | ~54 |
| `DSV_XM` cell: `DSV_SIZE` 28 → 30, across six classes | — | (in the above) |
| `DRV_MAX` 4 → 5: one `drv_tab` row and two name strings | ~40 | — |
| `SK_XMS` via a dispatch instead of `[xm_kb]` | ~10 | — |
| **net change** | **≈ −810** | **≈ −69** |

So roughly **−1,265 bytes of image** against the −1,544 a clean removal buys
— call it two of the three rungs, and the segment guard going from 460 bytes
left to about 1,300. Plus the two boot probes off every `kern_big` boot.

**Work, in commits:** the driver itself (mechanical — the code moves nearly
unchanged, `xm_arm` and the `cpu 386` islands included); the class and
dispatcher plumbing; the four slot bodies; the `xmcheck.py` rewrite; SPEC.md
§41 restructured and §51's class list extended. Call it a week of rounds with
the gate rewrite as the long pole. **SPEC.md goes first** — it is the binding
contract and this changes an interface.

---

## 5. The argument against, stated plainly

Everything in §3 and §4 says *this is buildable and affordable*. It is still
the wrong thing to build today, for one reason that no amount of plumbing
fixes.

**A driver is a bargain with the user: you tick a box, you get a capability.**
Sound works because the user can hear the difference and the Drivers page is
where they go to get it. The hard disk works because a drive icon appears.
The debug monitor works because somebody has deliberately gone looking for it.

Extended memory has no such loop. Nothing in the tree allocates from the pool
(the one consumer that ever existed, SPEC.md §53.6.1's fullscreen desktop
stash, was removed), so:

- the box is never ticked, because nothing ever fails in a way that suggests
  ticking it;
- an app that *did* want the store would call `OSAPI_XMEM_CAPS`, get 0, and
  degrade to its small-memory tier **in silence** — which is correct
  behaviour per SPEC.md §41.8 (*branch on the caps, never on the tier*) and
  is indistinguishable from "this machine has no extended memory";
- there is no affordance anywhere in an app's failure path that says *turn on
  Extended Memory in the Control Panel*, and inventing one is a bigger design
  than the driver.

So the honest description of the result is: **the four slots answer 0 on
every machine, and there is a file on the floppy that could change that if
anyone knew to ask.** That is a deletion with an escape hatch — which may
well be worth having, but it should be chosen as *that*, not as a memory
saving.

---

## 6. The two cheaper answers, if the bytes are the real problem

Both avoid inventing a driver class, and both should be considered first if
what actually hurts is `KERN_CODE_MAX`'s 460 bytes.

**(a) Move the code to `.cold`.** SPEC.md §2.6's cold segment is resident code
with a CS of its own and DS still `KERNEL_SEG`; it relieves `KERN_CODE_MAX`
and not `KERN_BUDGET`. The data — `xm_kb`, the pool base, `xm_gdt`,
`xm_gdtr` — stays in `.text`, because `xm_arm` reads the GDT register block
through DS. The arithmetic is deterministic: −1,034 from `.text` crosses two
image rungs (−1,024), +1,034 into `.cold` crosses two cold rungs (+1,024,
since cold has 352 bytes of slack), so **the footprint moves by roughly
nothing and the segment gains about a thousand bytes**. Cost: a `cw_*` shim
per crossing in each direction — the four API slots and `xm_owner`'s call out
to `snd_req_inst` — plus a far call on each, and `tools/os88ovlchk.py`
already refuses anything got wrong. **No ABI change, no new class, no gate
rewrite.**

**(b) Delete the feature and retire the four slots.** SPEC.md §20.8 rule 4
says the table is unfrozen while this tree hosts every package written for
this OS, and retirement is a mechanism the tree already has — 0x01E8 is the
worked example, and SPEC.md §20.3.1 lets a retired cell be reused later. This
takes the **full** −1,544 with no new class, no third dispatcher, no
`DSV_XM`, no gate to rewrite and no floppy file. It gives up the capability
outright, and a future consumer would pay to build it back — **into a driver,
which is the state §0 recommends anyway.**

Between them: **(a) if the segment guard is the pressure and the capability
should stay; (b) if the capability is genuinely surplus.** The driver is the
right answer to neither of those questions and the right answer to a third
one that nobody is asking yet.

---

## 7. When to revisit

The trigger is a **consumer**, and it is worth naming what one would look
like, because the answer changes the moment one exists:

- a package that wants a bulk store it cannot fit conventionally — Paint's
  undo image on a big canvas, Tracker's sample bank, Frotz's story file on a
  machine with the RAM but not the conventional RAM (SPEC.md §61.4 currently
  *refuses* a story that will not fit, with the arithmetic in the refusal);
- a kernel feature that wants one — SPEC.md §53.6.1's desktop stash was
  exactly this and was removed for reasons about *correctness*, not memory,
  so it is not coming back;
- a RAM disk, which is the shape that would make the Control Panel row make
  sense to a user for the first time: tick Extended Memory, get a volume.

The last of those is the interesting one, and it is worth noting that it
already has a home — `DRVC_FILE` (SPEC.md §62.9) exists and its comment names
*"a RAM disk"* as one of the shapes that class was written for. **A RAM disk
driver that owned its own store above 1MB would need none of this
conversion**, which is a real argument that §41's allocator is not the thing
a future consumer wants anyway.

Until then: the store stays a published ABI with no caller, in `kern_big`
only, which SPEC.md §41's own header already calls *"a deliberate state and
not an oversight"*.

---

## 8. Acceptance, if it is ever built

Recorded now so the next reader does not have to derive it:

- `make kernsplit`: `kern_small` **byte-identical** — this touches `kern_big`
  only, and a `kern_small` size that moves is docs/KERN-SPLIT-PLAN.md §2's
  whole failure mode.
- The four cells stay at 0x0190..0x01A8 and **`wm_geom` at 0x01B0 has the same
  body**, which is what says the table did not shift (SPEC.md §41.11.1's own
  test).
- One `.o88` still serves both kernels; `make small` still does not rebuild
  the apps disks.
- With the driver **absent**: all four slots answer tier 0's answers, register
  for register, on a cycle-accurate 8088 — the SPEC.md §41.11 comparison run
  again.
- With the driver **loaded** on a 386 machine: `tests/xmtest` +
  `tests/xmcheck.py` (rewritten per §3.7) still catch a missing force-free,
  verified the way that gate was verified the first time — by removing the
  teardown call and requiring the gate to FAIL.
- Once the driver is out, `kern_big` carries **no** `int 15h`, no port-0x92
  access and no `mov cr0` — SPEC.md §41.11.1's count, run against the build
  that still has the feature — and `XMEM.DRV` carries exactly the 2, 1+1 and
  2 of them that `kern_big` carries today.
- A settled desktop on a cycle-accurate 5150, driver row present and unticked,
  CGA at 60.0% lit.
