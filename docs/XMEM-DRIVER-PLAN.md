# Converting `xmem.inc` into a driver — an investigation

**Research document. Nothing here is built.** SPEC.md is the binding contract
for what the kernel *is*; this is the study of moving the store above 1MB
(SPEC.md §41) out of the kernel image and into a loadable `.DRV` (SPEC.md
§51), and the record of what was measured while asking.

The ask, in the requester's words:

- Investigate converting `xmem.inc` into a driver.
- **There is no point in a system without XMS reserving 1.2KB extra RAM
  forever.** It just does not have it and never will.
- **XMS will have consumers soon**, so it needs to be available.
- **The optimal situation would be no Control Panel interface.**

---

## 0. The verdict, up front

**Convert it, auto-load it from a boot probe, and give it no Control Panel
row.** All three are reachable, and the probe is the part that turns out
better than expected: **it is exact rather than heuristic**, because the
question *is there memory above 1MB* is answered by `int 15h` AH=88h, which
is already how `xm_init` sizes the store today. The sniff is not a proxy for
the answer — it is the answer.

The prize, measured rather than argued. Removing `xmem.inc` from `kern_big`
and leaving the tier-0 stubs the `%else` branch already carries:

```
kernsize[big]: sections   text 58,338 -1,034  bss 5,580 -124  ovl 2,752 -386   (sum -1,544)
kernsize[big]: rungs      image 64,000 -1,536 (82 left, was 460)
kernsize[big]: footprint  KERN_SIZE 101,376 of 104,960 -> 3,584 spare (7 steps), was 2,048
kernsize[big]: segment    .text+.bss 63,918 of KERN_CODE_MAX 65,536 -> 1,618 left
kernsize[big]: ladder     HEAP 0x1920 = 100.5 KB   (was 0x1980 = 102.0 KB)
kernsize[big]: *** the image rung CROSSED: 128 -> 125 steps of 512 ***
```

Net of the add-back (§6): about **−1,175 bytes of image**, which is the
1.2KB the ask names, and **two 512-byte rungs — 1,024 bytes of heap handed
back to every machine that has no XMS**, which is the machine this project is
calibrated against. `.text` + `.bss` goes from **460 bytes left of
`KERN_CODE_MAX` to about 1,220**, and that is the guard no conversation can
raise.

**What it costs the 8088:** `cmp byte [cpu_tier], CPU_8086` / `je`. Five
bytes in the boot overlay, which costs no RAM at all (SPEC.md §2.5), and four
clocks once.

Three things make this cheaper than it first looks, and each is worth
carrying into the build:

1. **The sniff is cheaper than today's boot path on every machine.** `xm_init`
   currently gates AH=88h behind the *verified A20 bit*, so a 286 with exactly
   1MB pokes port 0x92, races the keyboard controller for D1h/DFh and pays two
   bounded 65,536-poll timeouts before being told there is nothing up there.
   AH=88h needs none of that — it reports what the BIOS read out of CMOS. Ask
   it **first** and the whole A20 gate moves inside the driver's attach, where
   it only ever runs on a machine that has already said it has RAM to reach.
2. **The "no driver" implementation already exists and is already verified.**
   SPEC.md §41.11's `%else` branch is exactly the set of answers the four
   slots must give when nothing is published, and SPEC.md §41.11.1 records it
   being checked register-for-register against a real `kern_big` kernel on a
   cycle-accurate 8088. The conversion reuses those five bodies verbatim
   rather than inventing a refusal.
3. **Hiding the row costs zero bytes of table.** `DRVR_PAD` at offset 15 is a
   genuinely unused byte in every `drv_tab` row — grepped, no reader anywhere
   — so the flag goes there, exactly as `DV_PAD` became `DV_CLASS` in SPEC.md
   §18.7.3.

**The one thing to decide before building** is §7's question about what
happens on a machine that reports XMS and then cannot deliver it, because
with no Control Panel row there is nowhere for that to be said.

---

## 1. What is actually in the file

`kernel/xmem.inc` is 1,501 lines and four separable things:

| part | routines | section | what it is |
|---|---|---|---|
| **prerequisites** | `xm_a20_probe`/`_settle`, `xm_kbc_wait`, `xm_fast_a20`, `xm_kbc_a20`, `xm_a20_enable`, `xm_hma_claim` | `.ovl` | open and **verify** the A20 line; decide whether the HMA is claimed and therefore where the pool starts |
| **sizing** | `xm_init` | `.ovl` | `int 15h` AH=88h, clamp for tier 1's 24-bit descriptors, zero the block table, arm unreal mode, publish `[xm_kb]` last |
| **the allocator** | `xm_caps`, `xm_alloc`, `xm_free`, `xm_chk`, `xm_owner`, `xm_release_inst`, `xm_release_rec` | `.text` | 8 entries, 1KB granularity, owner-stamped, force-freed at instance teardown |
| **the transports** | `xm_copy`, `xm_arm`, `xm_ucopy`, `xm_bios` | `.text` | one ABI over two transports — unreal mode through FS/GS on tier 2, `int 15h` AH=87h on tier 1 |

Whole-module cost from `kernsize.py --modules`: **1,040 bytes of `.text` and
124 of `.bss`**, plus the `.ovl` half, which the §0 measurement puts at
**386**.

Above it sit four published slots — `OSAPI_XMEM_CAPS` / `_ALLOC` / `_FREE` /
`_COPY` at 0x0190..0x01A8 — and exactly five kernel-side readers:
`memory.inc`'s one `mov ax, [xm_kb]` for `SK_XMS`; three
`call xm_release_rec` at SPEC.md §29.4's teardown sites in `instance.inc`;
one more through `loader.inc`'s cold shim; `kmain`'s two overlay calls; and
`tests/sysbench` + `tests/xmtest` from outside.

That is the whole blast radius. **The feature is already almost detached** —
SPEC.md §41.11 detached it from `kern_small` a round ago and `xmem.inc` has
one `%ifdef` around the entire body.

---

## 2. The precedent is exact, and it is the sound layer

`snd.inc` is the same conversion already done: **the kernel kept the ABI and
the policy, the hardware went to a driver.** `osapi_snd_fm` (slot 0x00F8)
stays resident, stamps the requesting instance into DH out of kernel state
the driver cannot see, and far-calls `DSV_FM` through `drv_svc_call`. The
card code is `SOUND.DRV`, and a machine with no card carries a `drv_tab` row
and a file it never reads.

| stays in the kernel | goes to `XMEM.DRV` |
|---|---|
| the four cells at 0x0190..0x01A8, and their tier-0 answers when nothing is published | their real bodies |
| stamping *who is asking* (`xm_owner` → `snd_req_inst`) | the allocator and its table |
| raising `[sch_lock]` around the transport (§5.2) | both transports, the GDT, the unreal-mode arm |
| `SK_XMS`, via a dispatch | A20, the HMA claim, `int 15h` AH=88h |
| `xm_release_rec`, as a `DSV_RELINST` dispatch | the force-free walk behind it |
| the boot sniff (§3) | — |

**The two things that must NOT move** are the slot numbers and the register
contracts (SPEC.md §20.8 rule 4 — the table is unfrozen in alpha, but a
re-contracted cell at an old number is the failure that assembles cleanly and
runs wrong). A package sees no difference at all.

---

## 3. The boot probe: cheap, exact, and earlier than the gate

### 3.1 The probe

```
xm_sniff:                       ; .ovl, called from kmain before drv_boot
    cmp byte [cpu_tier], CPU_8086
    je  .none                   ; the target machine leaves here. 4 clocks.
    mov ah, 0x88
    int 0x15                    ; AX = KB above 1MB
    jc  .none                   ; no such service
    or  ax, ax
    jz  .none                   ; a 1MB AT
    mov byte [drv_tab + XMROW*DRVR_SIZE + DRVR_WANT], 1
.none:
    ret
```

**Why this is exact and the sound sniff is not.** `drv_snd_sniff` runs an
OPL2 timer dance that is *correlated* with the driver finding a card; it can
be wrong in both directions, which is why SPEC.md §51.3.1 needed the
`SNDSNIFF=sb` knob for a card whose FM half is jumpered off. Here the sniff
runs **the same `int 15h` AH=88h that `xm_init` sizes the store with**. A
machine the sniff skips is a machine that would have published `[xm_kb] = 0`
anyway, so there are no false negatives by construction — not by testing.

**The one false positive**, and it should be written into the spec rather
than discovered: a machine whose BIOS reports KB but whose A20 gate will not
*verify*. Today that ends as `xm_kb = 0`. Under this design the driver loads,
fails A20 at attach, refuses, and the kernel frees the image — the same
answer, having spent one four-sector read. On a machine that broken, that is
the right price.

### 3.2 Reordering AH=88h ahead of the A20 gate is a strict improvement

`xm_init` today tests `CPU_F_A20` and only then issues AH=88h, on the
reasoning that *"the gate never verified: nothing up there is reachable,
whatever the CPU is"*. That is a correct gate on **usability** and it is not
a dependency: AH=88h reports what the BIOS read from CMOS and touches no
address above 1MB.

So the order inverts, and the expensive half moves:

| machine | today | proposed |
|---|---|---|
| 8088 (the target) | one `cmp` | one `cmp` — and 1.2KB back |
| 286+, exactly 1MB | port 0x92 poke, KBC D1h/DFh race, two 65,536-poll timeouts, then AH=88h | one `int 15h` |
| 286+ with XMS | the same gate, then AH=88h, then unreal-mode arm — all resident | one `int 15h`, then ~4 sectors of driver (~100 ms at PERFORMANCE.md Set 24's ~24 ms/512B), then the gate inside attach |

The keyboard-controller sequence is the one with a documented reboot hazard
in it (SPEC.md §41.2 — a stray byte to 0x60 between the D1h and the DFh can
land on the CPU reset line). **Confining it to machines that have already
reported extended memory is worth having on its own.**

### 3.3 Where it lives, and why not in `drv_boot`

`.ovl`, called from `kmain` at exactly the point `ovl_xm_a20` and
`ovl_xm_init` are called today — which is `drv_snd_sniff`'s own home, and for
`drv_snd_sniff`'s own reason: **`drv_boot`'s first mount is what writes over
the overlay**, so a probe called from there would be running FAT. The
overlay costs no RAM at all, so the sniff is free of both guards.

Setting `DRVR_WANT` from the overlay makes it a **default**, not an override
— but here, unlike sound, there is no user opinion for a settings file to
carry, which §4 is about.

### 3.4 "Early, and only there" — yes on both counts

`drv_load` needs a mounted A:, the claim heap and the FAT window, so
`drv_boot` is the earliest legal home; it runs **before the first
`wm_paint_all`**, so the store is live from frame one, exactly as sound is.
Nothing in the kernel wants XMS before that, and a consumer is a package the
user launches seconds later at the earliest.

And it is the *only* way in: the load is gated on the sniff, and with no
Control Panel row (§4) and no `SYSTEM.CFG` bit there is no second door.

---

## 4. No Control Panel interface

### 4.1 The row is hidden, and it goes LAST

Two independent constraints point at the same placement, which is the sign it
is the right one:

- **SPEC.md §51.2.1**: *"The new row goes at the END of `drv_tab`. Row order
  is not cosmetic: `SYSTEM.CFG`'s driver bitmap is one bit per row index, so
  inserting a class in the middle would renumber every user's saved
  settings."*
- **SPEC.md §31.10.1**, the Control Panel's own Display page: *"the Display
  item is LAST in `cp_items` precisely so that hiding it is a shorter list
  rather than a remapping every row below it would have to agree about."*

That second one is load-bearing here, because `cp_drv_click` **divides** the
click's y to get a row index (*"the hit bands are DERIVED from `DRV_MAX`
rather than enumerated"*). A hidden row anywhere but last would put the
display index and the `drv_tab` index permanently out of step, which is
SPEC.md §19.4/§22.8's whole family of bugs in a new place.

Hidden last, the page simply draws and hit-tests one row fewer.

### 4.2 The discipline: `DRV_MAX` is the LIFECYCLE count

This is the rule to write down, because getting it backwards is silent:

> **Hidden is a property of the USER INTERFACE, not of the driver's life.**
> The page and the settings file walk a shorter list; everything that loads,
> unloads, dispatches, fences or *accounts* still walks `DRV_MAX`.

| must skip the hidden row | must NOT skip it |
|---|---|
| `cp_drv_paint` / `cp_drv_click` — the list, the count, the hit bands | `drv_seg_scan` / `drv_owns_seg` — or the image is charged to nobody and the Task Manager's System total is short by its size (SPEC.md §51.1) |
| `drv_want_get` / `drv_want_set` — the `SYSTEM.CFG` bitmap | `drv_row`, `drv_load`, `drv_attach`, `drv_unload` |
| `drv_nerr` / `drv_notice` — §7 | `mem_sum_kb`'s `drv_owns_seg` question |

Because the row is last, the two skips on the left are `DRV_MAX - 1` rather
than a test inside a loop — and the bit index of every existing row is
untouched, so no user's saved settings move.

`DRVR_PAD` (offset 15, unused in every row — grepped) carries the flag, so
the row is still 16 bytes and `index << 4` is still how it is reached.

### 4.3 What interface remains, and it is the right one

The driver publishes **no `DSV_CPNAME`**, so SPEC.md §31.9's per-driver page
does not appear either — that falls out of the publication slot rather than
needing arranging.

What is left is the Task Manager's `XMS used/sizedK` line (SPEC.md §41.6.1),
which is **read-only and already there**. That is the correct amount of user
interface for a thing with no user-visible cost and no decision to make: it
reports, and it cannot be got wrong.

For development, the tree's standard shape is a build knob for the A/B —
`make XMEM=0` alongside `FLOPPY1=1`, `DIRW1=1` and `DISKAL=1` — which removes
the sniff rather than just the call, so "is this regression the driver" is one
boot instead of a bisect.

---

## 5. What was checked, and what it answered

### 5.1 Boot ordering is not a blocker — this was the expected wall

`kmain` calls `ovl_xm_a20` and `ovl_xm_init` **before `sched_init`**, and its
comment says why: *"this is the last moment at which no kernel ISR is
installed — the unreal-mode window inside `xm_init` runs with CR0.PE set and
a real-mode IVT."* A driver attaches at `drv_boot`, long after.

**The file itself is the refutation.** `xm_arm` masks NMI at port 0x70 and
runs the entire PE window inside one `pushf`/`cli` … `popf`, and it is
**already called at run time from `xm_ucopy`**, once per 1KB chunk, with
`int 08h` hooked and IRQ3/IRQ4 live. If a kernel ISR could break the
transition, the shipped transport has been broken since it was written. The
early call is a comfort, not a constraint.

Two neighbours checked with it:

- **The A20 probe's scratch stays free.** It writes one word at linear
  `0x0500` and one through the alias at `HMA_SEG:0510`, saving and restoring
  both under `cli`. `KERNEL_SEG` is 0x0060 so the kernel starts at 0x600, and
  SPEC.md §18.92's diskette parameter table is at `0000:0580`. The probe
  touches `0x500..0x501` and nothing else.
- **The GDT works from a heap segment.** `xm_arm` computes the `lgdt` base
  from DS at run time rather than baking `KERNEL_SEG` in, and a driver image
  is an ordinary heap claim well under 1MB, so the 24 base bits a 16-bit
  `lgdt` loads are still exact.

### 5.2 `[sch_lock]` has no API slot — and the kernel is where it belongs

Both transports raise the scheduler lock. A driver cannot:
`drivers/debug/debug.asm` records this in its own header as the reason it has
no `call` verb and no disk payload channel — *"and `[sch_lock]` has no API
slot. Adding one is kernel code."*

**The resolution needs no new slot and is already in the tree.** `dsk_xfer`
raises `[sch_lock]` *before* dispatching `DSV_BLK`, so a block driver is
handed a locked scheduler rather than taking one (`drivers/net/net.asm` says
so twice). The kernel's `xm_copy` cell does the same around its dispatch. The
driver keeps its own per-chunk `cli` window, which protects against a
different thing — an ISR, not a task switch — and needs nothing.

The consequence is an improvement: **SPEC.md §41.8's context rule becomes the
kernel's to enforce rather than the driver's to document.**

### 5.3 `DSV_*` is full, so this needs a sub-table

`DSV_SIZE` is 28 — fourteen words, all spoken for. `DSV_FS` (SPEC.md §62.9.1)
is the precedent and its reasoning holds: the table is copied **per class**
into `.bss`, so a cell costs `2 × DRVC_MAX` on every machine including the
ones with no such driver. So one `DSV_XM` pointer to an `XMV_*` table in the
driver's own segment — `XMV_CAPS`, `XMV_ALLOC`, `XMV_FREE`, `XMV_COPY` — and
reuse the existing `DSV_RELINST`, which is already generic and already per
class.

### 5.4 A third dispatcher body, and take the class

`drv_svc_call` names `drv_fptr`/`drv_fseg` outright — **the sound class** —
because SPEC.md §51.2.1's register argument leaves it nothing to carry a
class in. `drv_blk_call` is the second body and takes its class from
`[drv_blkcls]`. `xm_copy` spends ES:SI, DX:AX, CX and DI, so AL is not free
here either: it wants the memory-cell shape. **Twenty bytes.**

**Do not try to save the class by dispatching off the `drv_tab` row.** It
looks tempting — `DRVR_DISP`/`DRVR_SEG` are adjacent precisely so
`call far [bx+DRVR_DISP]` works, and the row is a compile-time constant. But
`drv_load` arms `DRVR_SEG` **before** `drv_attach` runs, so the row is a live
far pointer into a driver that has not probed yet and may be about to refuse.
`drv_publish` arms the class slot only *after* attach returns, and that
ordering is the fence SPEC.md §51.2.2 exists to describe. The saving is 58
bytes against a 1,544-byte removal; the tree has already been bitten twice by
publication shortcuts (§51.2.1's shared slot, §51.2.2's `DRVV_READY`
fallthrough). Take the class.

### 5.5 Four things that need no new mechanism at all

- **`xm_owner` needs no slot.** `osapi_snd_fm`'s header states the rule: the
  requesting instance is stamped in the kernel because `snd_req_inst` reads
  kernel state, and a driver that had to ask would need a slot of its own.
- **`xm_release_rec` fits `DSV_RELINST`.** `snd_release_inst` already
  dispatches that cell from the dying task at SPEC.md §29.4's teardown sites.
  `instance.inc` keeps calling one kernel routine and never learns anything
  moved — the same argument SPEC.md §41.11 made for keeping a `ret` in
  `kern_small` rather than three `%ifdef`s in a file that is not about
  extended memory.
- **`[cpu_feat]` needs no slot.** SPEC.md §41.11 established by grep that
  `CPU_F_A20` / `_HMA` / `_UNREAL` have no readers outside `cpudet.inc` and
  `xmem.inc`. The bits stop being written when the driver is absent and
  `cpu_info`'s AH answers 0 — exactly what `kern_small` answers today, and
  the truth on a machine with no gate verified.
- **The floppy has room.** The 360KB system disk is at 149 of 354 clusters
  and an `XMEM.DRV` of ~1.6KB is four of them.

### 5.6 Three genuinely new obligations

- **A20 outlives detach.** `DRVV_DETACH` cannot fail and is documented as
  *restore the ports you saved at attach*. The A20 line is machine state, not
  a hooked resource: nothing in os8088 depends on 1MB wraparound, so leaving
  it open is safe and re-closing it buys nothing. **Say so in the spec**, or
  a later reader implements the tidy-looking version and changes HMA aliasing
  under a BIOS that has opinions.
- **Unreal mode outlives detach too.** FS and GS keep their 4GB limits after
  the image is freed. Harmless — a wider limit faults nothing — but SPEC.md
  §41.4's rule becomes *only `XMEM.DRV` writes FS or GS*, and it must now hold
  across a window in which the driver is **not loaded** and the limits are
  still wide. Same grep, restated rather than inherited.
- **Detach must force-free the whole block table**, because every outstanding
  block is about to lose its allocator. Four lines, and with no Control Panel
  row nothing can trigger it — which is a reason to write it correctly, not a
  reason to skip it.

### 5.7 The gate breaks, and it is the only one there is

`tests/xmcheck.py` reads `xm_tab` out of the running guest by kernel symbol
and diffs it across a package's teardown. It is the only thing exercising the
allocator and the force-free at all, and it exists because those three
`call xm_release_rec` sites were **silently absent for a year** after an
integration merge dropped them.

Moved into a driver, `xm_tab` is at a heap segment known only at run time.
The gate is rewritable — read the row's `DRVR_SEG` from the kernel's map,
then the table at that segment plus a fixed offset from the driver's own map
— but it is real work on the one instrument standing between this feature and
the exact failure it has already suffered once. **Budget it as part of the
conversion, not after.**

---

## 6. What it costs

Measured where it could be measured, hand-counted where it could not.

**Removed from the kernel** (measured, §0): `.text` −1,034, `.bss` −124,
`.ovl` −386.

**Added back** (estimated):

| item | `.text` | `.bss` | `.ovl` |
|---|---|---|---|
| four slot bodies: stamp DH, raise `[sch_lock]`, dispatch, else the §41.11 answers | ~150 | — | — |
| a third dispatcher body (§5.4) | ~20 | 1 | — |
| `DRVC_MEM` = 6: `drv_fptr6`/`drv_fseg6`, `DRVC_MAX` 5 → 6, `DSV_SIZE` 28 → 30 | 4 | ~54 | — |
| `DRV_MAX` 4 → 5: one `drv_tab` row and two name strings | ~40 | — | — |
| `xm_sniff` (§3.1) | — | — | ~40 |
| `SK_XMS` and `xm_release_rec` as dispatches | ~30 | — | — |
| the four hidden-row skips (§4.2) | ~30 | — | — |
| **net change** | **≈ −760** | **≈ −69** | **≈ −346** |

So roughly **−1,175 bytes of image** — the ask's 1.2KB — against the −1,544 a
bare removal buys. Expect **two of the three rungs**: 1,024 bytes of heap
handed back to every machine, and `KERN_CODE_MAX` going from 460 bytes left
to about 1,220.

**Work, in commits:** SPEC.md first (§41 restructured, §51's class list
extended, §4.2's discipline written down) — it is the binding contract and
this changes an interface. Then the driver itself, which is mechanical: the
code moves nearly unchanged, `xm_arm` and the `cpu 386` islands included.
Then the class and dispatcher plumbing; the four slot bodies; the sniff and
the hidden row; the `xmcheck.py` rewrite. The gate is the long pole.

---

## 7. The one open question

**On a machine that reports XMS and then cannot deliver it, nothing can say
so.** With no Control Panel row there is no `drv_notice`, no caption, and no
place a `DRVE_*` can be read. Three ways it happens: A20 will not verify
(§3.1's false positive), the heap cannot fund the image (`DRVE_MEM`), or the
`.DRV` is missing from the system disk (`DRVE_NOENT` — a hand-built floppy,
or a `make` that shipped the kernel and not the driver).

Three answers, and this is the requester's call:

1. **Silence.** The Task Manager's XMS line reads 0, which is what SPEC.md
   §41.8 already tells every package to branch on. Consistent, and invisible
   in exactly the case a user might want to act on.
2. **A toast** (SPEC.md §59). One line in the menu bar, no window, no page to
   send anybody to — `Extended memory unavailable` — and it retires itself.
   This is the closest thing to "no Control Panel interface" that still says
   something, and the mechanism is already there.
3. **The row unhides on failure only.** Cute, and wrong: it makes the page's
   row count depend on run-time state, which is §4.1's index-drift bug with a
   trigger nobody will reproduce.

**Recommend 2**, and only for a row whose sniff said yes — a machine that
never had XMS must stay entirely silent, which falls out of `DRVR_WANT` being
0 there.

---

## 8. Acceptance

- `make kernsplit`: `kern_small` **byte-identical**. This touches `kern_big`
  only, and a `kern_small` size that moves is docs/KERN-SPLIT-PLAN.md §2's
  whole failure mode. (`kern_small` keeps its `%ifdef`: its floor machine is
  an 8088, so it should carry neither the row nor the sniff.)
- The four cells stay at 0x0190..0x01A8 and **`wm_geom` at 0x01B0 has the
  same body**, which is what says the table did not shift (SPEC.md §41.11.1's
  own test).
- One `.o88` still serves both kernels; `make small` still does not rebuild
  the apps disks.
- **On an 8088**: all four slots answer tier 0's answers register-for-register
  — the SPEC.md §41.11.1 comparison re-run — the sniff sets no `DRVR_WANT`,
  and **no sector of `XMEM.DRV` is read**, checked with `os88marty.py`'s disk
  counters from outside the guest.
- **On a 286+ with XMS**: the driver loads without a `SYSTEM.CFG` asking,
  `OSAPI_XMEM_CAPS` reports the same KB the pre-conversion kernel reported on
  the same machine, and `tests/xmtest` + `tests/xmcheck.py` (rewritten per
  §5.7) still catch a missing force-free — verified the way that gate was
  verified the first time, by removing the teardown call and requiring the
  gate to FAIL.
- **The Drivers page shows four rows, not five**, and a `SYSTEM.CFG` written
  before the conversion still restores every row's tick to what it was.
- Once the driver is out, `kern_big` carries **no** `int 15h` outside the
  sniff, no port-0x92 access and no `mov cr0` — SPEC.md §41.11.1's count run
  against the build that still has the feature.
- A settled desktop on a cycle-accurate 5150 with the real IBM Oct-82 BIOS:
  CGA at 60.0% lit, and the Task Manager opens and reads `XMS 0/0K`.
