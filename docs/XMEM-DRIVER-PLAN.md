# Taking the store above 1MB out of the kernel image — an investigation

> **BUILT. SPEC.md §41.12 is the contract; this is the study that produced
> it.** What shipped is §0's recommendation with one change of shape found
> while writing it — the overlay is loaded through `drv_load_at`, a two-line
> factoring of `drv_load`, rather than through anything new. Measured on the
> tree: `.text` −668, `.bss` −102, `.ovl` −357 = **−1,127 bytes**, two
> 512-byte rungs, footprint spare 2,048 → 3,072, `KERN_CODE_MAX` 460 bytes
> left → **1,230**, and the heap ladder 102.0 → 101.0 KB. `kern_small` is
> **byte-identical** (md5 unchanged). §10 is what the build found that the
> investigation did not.

**Research document.** SPEC.md is the binding contract
for what the kernel *is*; this is the study of moving the store above 1MB
(SPEC.md §41) out of the kernel image and loading it on demand, and the record
of what was measured while asking.

The ask, in the requester's words:

- Investigate converting `xmem.inc` into a driver.
- **There is no point in a system without XMS reserving 1.2KB extra RAM
  forever.** It just does not have it and never will.
- **XMS will have consumers soon**, so it needs to be available.
- **The optimal situation would be no Control Panel interface.**
- Fix the "must be last in order to hide" constraint that a previous driver
  already had to pay.
- **Could it be cheaper as something other than a driver?**
- Failing silent is fine — bad memory is the system's fault, not the user's,
  and they do not need telling at every boot. A System Info (CheckIt) type app
  is the right future surface.

---

## 0. The verdict, up front

**Take it out of the kernel, auto-load it from a boot probe, and make it an
OVERLAY rather than a driver.** All three asks land, and the last question is
the one that pays: **it should not be a driver, and the mechanism for
"driver-format image the kernel does not manage as a driver" already exists
and ships** — `OS88_OVERLAY` / `DRVC_OVL`, which is exactly what
`HDDTOOL.DRV` is (SPEC.md §52.11, and §2.2 below: the requester's memory was
right, it *is* the hard drive).

That single change deletes the most expensive and most dangerous part of the
first draft of this plan. There is no `drv_tab` row, so there is no hidden
flag, no "must be last" constraint, no `SYSTEM.CFG` bitmap interaction, no
`DRV_MAX`-versus-visible-count discipline and no four skip sites to get wrong.
**The cheapest part of this plan is the part that stopped existing.**

The probe is the other good news: **it is exact, not heuristic.** *Is there
memory above 1MB* is answered by `int 15h` AH=88h, which is already how
`xm_init` sizes the store — so unlike `drv_snd_sniff`, whose OPL2 timer dance
is merely correlated with the driver finding a card, this probe **is** the
question.

Measured. Removing `xmem.inc` from `kern_big` and leaving the tier-0 stubs the
`%else` branch already carries:

```
kernsize[big]: sections   text 58,338 -1,034  bss 5,580 -124  ovl 2,752 -386   (sum -1,544)
kernsize[big]: rungs      image 64,000 -1,536 (82 left, was 460)
kernsize[big]: footprint  KERN_SIZE 101,376 of 104,960 -> 3,584 spare (7 steps), was 2,048
kernsize[big]: segment    .text+.bss 63,918 of KERN_CODE_MAX 65,536 -> 1,618 left
kernsize[big]: ladder     HEAP 0x1920 = 100.5 KB   (was 0x1980 = 102.0 KB)
kernsize[big]: *** the image rung CROSSED: 128 -> 125 steps of 512 ***
```

Net of the add-back (§7): about **−1,240 bytes of image** — the 1.2KB the ask
names — and **two 512-byte rungs, 1,024 bytes of heap handed back to every
machine with no XMS**, which is the machine this project is calibrated
against. `.text` + `.bss` goes from **460 bytes left of `KERN_CODE_MAX`** to
about **1,270**, and that is the guard no conversation can raise.

**What it costs the 8088:** `cmp byte [cpu_tier], CPU_8086` / `je`. Five bytes
in the boot overlay, which costs no RAM at all (SPEC.md §2.5), and four clocks
once.

**On the "must be last" constraint:** it is real, it has been paid three times
in three different tables (§5), and **this plan no longer spends it a fourth
time** — which is a better outcome than fixing it. The general fix is still
worth doing and §5.3 scopes it, but as its own work, uncoupled from this.
Making it a prerequisite would make the cheap plan depend on a refactor it
does not need.

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
`call xm_release_rec` at SPEC.md §29.4's teardown sites in `instance.inc`; one
more through `loader.inc`'s cold shim; `kmain`'s two overlay calls; and
`tests/sysbench` + `tests/xmtest` from outside.

That is the whole blast radius. **The feature is already almost detached** —
SPEC.md §41.11 detached it from `kern_small` a round ago and `xmem.inc` has
one `%ifdef` around the entire body.

---

## 2. Two precedents, and they do different halves of the job

### 2.1 `snd.inc` — how to split the ABI from the implementation

The kernel kept the ABI and the policy; the hardware went to a loadable image.
`osapi_snd_fm` (slot 0x00F8) stays resident, stamps the requesting instance
into DH out of kernel state the loaded code cannot see, and far-calls through
a pointer armed only after the image said yes.

| stays in the kernel | goes to the loaded image |
|---|---|
| the four cells at 0x0190..0x01A8, and their tier-0 answers when nothing is loaded | their real bodies |
| stamping *who is asking* (`xm_owner` → `snd_req_inst`) | the allocator and its table |
| raising `[sch_lock]` around the transport (§6.2) | both transports, the GDT, the unreal-mode arm |
| `SK_XMS`, via a dispatch | A20, the HMA claim, `int 15h` AH=88h |
| `xm_release_rec`, as a dispatch | the force-free walk behind it |
| the boot sniff (§3) | — |

**The two things that must NOT move** are the slot numbers and the register
contracts (SPEC.md §20.8 rule 4 — the table is unfrozen in alpha, but a
re-contracted cell at an old number is the failure that assembles cleanly and
runs wrong). A package sees no difference at all.

### 2.2 `HDDTOOL.DRV` — how to be a loadable image and NOT a driver

This is the precedent that changes the plan, and it is the hard-disk one the
requester remembered. SPEC.md §52.11: *"**The tool is not a driver, and could
not be one.** Publication is per CLASS (§51.2.1), so a second `DRVC_DISK`
image would disconnect the very transport it needs. It has the same header,
the same `org 0`, the same three-byte dispatcher and the same one-claim load
discipline — and no row in `drv_tab`, no Drivers-page tick, and a class byte
(`DRVC_OVL`) the kernel deliberately does not know."*

That is precisely the shape XMEM wants, and every one of its properties is one
XMEM needs:

- **No `drv_tab` row** → no bitmap bit, no Drivers-page row, no hidden flag,
  no positional constraint.
- **No publication class** → `DRVC_MAX` stays 5; no `drv_cpname` slot wasted
  on a page that will never exist.
- **`DRVC_OVL` = 0x40, out of range** → `drv_cls_idx` refuses it by arithmetic,
  so it can never take a publication slot even by mistake. (It was 4 and
  *moved* when `DRVC_NET` took that number — which is the "special position to
  stay invisible" the requester was thinking of. It is a class number rather
  than a row index, but it is the same trick.)
- **Header version stays 4** → the application loader refuses it and it can
  never be double-clicked (SPEC.md §51.1).
- **`.DRV` suffix** → `os88disk.py` gives it read-only + hidden + system for
  free (SPEC.md §19.6), so it is invisible in the Disk window and to DOS
  `DIR`, and the installer's "every `*.DRV`" copy picks it up.

The one thing XMEM's overlay does that HDDTOOL's does not: **its owner is the
kernel, not another driver.** That is the only new idea in this plan, and §4
is what it costs.

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
    mov byte [xm_want], 1
.none:
    ret
```

**Why this is exact and the sound sniff is not.** `drv_snd_sniff` runs an OPL2
timer dance that is *correlated* with the driver finding a card; it can be
wrong in both directions, which is why SPEC.md §51.3.1 needed the
`SNDSNIFF=sb` knob for a card whose FM half is jumpered off. Here the sniff
runs **the same `int 15h` AH=88h that `xm_init` sizes the store with**. A
machine the sniff skips is a machine that would have published `[xm_kb] = 0`
anyway, so there are no false negatives by construction — not by testing.

**The one false positive**, and it belongs in the spec rather than in a
discovery: a machine whose BIOS reports KB but whose A20 gate will not
*verify*. Today that ends as `xm_kb = 0`. Here the overlay loads, fails A20 at
entry, refuses, and the kernel frees the image — the same answer, having spent
one four-sector read. On a machine that broken, that is the right price, and
§8 is why nobody is told.

### 3.2 Reordering AH=88h ahead of the A20 gate is a strict improvement

`xm_init` today tests `CPU_F_A20` and only then issues AH=88h, on the
reasoning that *"the gate never verified: nothing up there is reachable,
whatever the CPU is"*. That is a correct gate on **usability** and it is not a
dependency: AH=88h reports what the BIOS read from CMOS and touches no address
above 1MB.

So the order inverts, and the expensive half moves:

| machine | today | proposed |
|---|---|---|
| 8088 (the target) | one `cmp` | one `cmp` — and 1.2KB back |
| 286+, exactly 1MB | port 0x92 poke, KBC D1h/DFh race, two 65,536-poll timeouts, then AH=88h | one `int 15h` |
| 286+ with XMS | the same gate, then AH=88h, then the unreal-mode arm — all resident | one `int 15h`, then ~4 sectors (~100 ms at PERFORMANCE.md Set 24's ~24 ms/512B), then the gate inside the load |

The keyboard-controller sequence is the one with a documented reboot hazard in
it (SPEC.md §41.2 — a stray byte to 0x60 between the D1h and the DFh can land
on the CPU reset line). **Confining it to machines that have already reported
extended memory is worth having on its own.**

### 3.3 Where it lives, and why not in `drv_boot`

`.ovl`, called from `kmain` at exactly the point `ovl_xm_a20` and
`ovl_xm_init` are called today — which is `drv_snd_sniff`'s own home, and for
`drv_snd_sniff`'s own reason: **`drv_boot`'s first mount is what writes over
the overlay**, so a probe called from there would be running FAT. The overlay
costs no RAM at all, so the sniff is free of both guards.

### 3.4 "Early, and only there" — yes on both counts

The load needs a mounted A:, the claim heap and the FAT window, so `drv_boot`
is the earliest legal home; it runs **before the first `wm_paint_all`**, so the
store is live from frame one. Nothing in the kernel wants XMS before that, and
a consumer is a package the user launches seconds later at the earliest.

And it is the *only* way in: the load is gated on the sniff, and with no
`drv_tab` row and no `SYSTEM.CFG` bit there is no second door.

---

## 4. Not a driver: what the overlay shape actually costs

### 4.1 The kernel needs a row-shaped record, not a row in the table

`drv_load` already does everything the load needs — `drv_vol_bank` … the
mount, `drv_find`, the size check, **exactly one** `mem_claim` at
`MEM_K_DRV`, the read, `drv_check`, the far call to the entry, and
`drv_vol_back` to put the user's volume back (SPEC.md §51.5.2). None of that
should be written twice; a second loader is the shape that rots.

**It is a two-line factoring, not surgery.** `drv_load` opens with
`call drv_row` to turn an index into `BX = the row for the whole routine`.
Split it there:

```
drv_load:       call drv_row        ; index -> BX
drv_load_at:    ...                 ; BX = ANY 16-byte DRVR_-shaped record
```

and point the second at `xm_row`, sixteen bytes in `.text` that are not in
`drv_tab`. Everything downstream is reused verbatim.

**`drv_check` needs no change at all**, which is the part that makes this
cheap: it compares the image's class byte against `[bx+DRVR_CLASS]` — *the
row's own class* — rather than against a list of known classes. Give `xm_row`
`DRVC_OVL` and an overlay-shaped image validates with zero new code, while
`drv_cls_idx` still refuses that number by range so it can never take a
publication slot.

### 4.2 Dispatch is one word pair, not a class

The four slots dispatch through `xm_fptr`/`xm_fseg` in `.bss`, armed **after**
the entry returns CF=0 and left at 0 otherwise — the same fence
`drv_publish` provides, in one word instead of a class. The entry returns
`SI = an XMV_* table` which the kernel stages into ~10 bytes, exactly as
`drv_svc` is staged, but without growing `DSV_SIZE` by 2 across six classes.

`drv_svc_call` is not reused: it names the sound class outright (SPEC.md
§51.2.1's register argument leaves it nothing to carry a class in). A
14-byte body dispatching through one fixed pointer replaces it, and there is
no third class-dispatch body to add.

### 4.3 Two things it gives up, and only one is real

- **`drv_status`, the Drivers-page caption, `drv_unload`, `DRVV_TIER`,
  `DRVV_READY`.** All unwanted: there is no user management, no tiers, and no
  fence to wait for — XMEM publishes nothing through `OSAPI_VOL_*` or
  `OSAPI_DRV_TASK`, which are the only things `DRVV_READY` exists for.
- **`drv_seg_scan` / `drv_owns_seg` will not find the image**, since they walk
  `drv_tab`. This *sounds* like it breaks the Task Manager's accounting and
  does not: `mem_sum_kb` counts the image by its **owner tag**
  (`cmp word [ss:si+MC_OWN], MEM_K_DRV`, memory.inc:1637), not by table
  membership. `drv_owns_seg` exists for the **bulk buffers a driver claims for
  itself**, which carry the driver's own segment as owner.

  **So this is an invariant to write into the spec, because it is checkable
  and it will not announce itself:** `XMEM.DRV` may claim nothing bulk. Its
  block table is 64 bytes inside its own image. The day it wants a heap claim
  of its own is the day it needs a `drv_tab` row back — or `mem_own_drv` needs
  to know about `xm_row`.

### 4.4 What this deletes from the first draft

Every one of these was in the plan a revision ago and is now simply gone:

- `DRVR_HIDDEN` and the hunt for a free bit;
- "the row must be last", and the argument with §5's two other claimants;
- the four skip sites (`cp_drv_paint`, `cp_drv_click`, `drv_want_get`/`_set`,
  `drv_nerr`) and the "`DRV_MAX` is the lifecycle count" discipline that had to
  be written down so nobody skipped the wrong one;
- `DRVC_MEM`, `DRVC_MAX` 5 → 6, and ~54 bytes of `.bss` across four per-class
  arrays including a `drv_cpname` slot for a page that would never exist;
- `DSV_SIZE` 28 → 30 across every class;
- a third `drv_svc_call` body.

---

## 5. The "must be last" constraint: three claimants, one shape

The requester is right that this has been paid before. It has been paid
**three times, in three different tables**, and they are not the same rule —
which is why the fix is not one change.

### 5.1 The three

1. **The Control Panel's Display page** — the literal one.
   `kernel/ctrl.inc:167`: *"DISPLAY IS LAST, AND THAT IS WHAT MAKES IT
   HIDEABLE (SPEC.md §31.10.1). … 'hidden' for a row in the MIDDLE of this
   table would mean every row below it maps to a different record, which is a
   second opinion about what the user clicked on at four call sites."* A
   machine with one adapter has nothing to choose between, so `[cp_nst]` falls
   by one and row → record stays the identity.
2. **`HDDTOOL.DRV`'s class number** — the hard-disk one (SPEC.md §52.11).
   `DRVC_OVL` was 4, and moved to 0x40 when `DRVC_NET` took that number,
   specifically to sit *outside* `DRVC_MAX` so `drv_cls_idx` refuses it by
   range. A numeric position standing in for "this is not a managed driver".
3. **`drv_tab` appends** (SPEC.md §51.2.1) — *"Row order is not cosmetic:
   `SYSTEM.CFG`'s driver bitmap is one bit per row index, so inserting a class
   in the middle would renumber every user's saved settings."*

### 5.2 Only one of them is a bug, and it is the first

**(3) is not fixable and should not be**: it is an append-only file format,
and append-only is the correct discipline for a settings file that survives
across versions. **(2) is not a constraint any more** — it is a discriminant
that works, and §2.2 shows it is exactly the property XMEM wants to reuse.

**(1) is the real one**, and its cause is precise: `cp_drv_paint` **walks**
rows computing y from the index (`cp_drv_rowy`, a multiply), while
`cp_drv_click` **divides** the click's y to recover the index. Those are
inverse functions maintained in two places — so a hole in the middle cannot be
expressed, because a divide cannot skip one. That is SPEC.md §22's `fm_hit`
discipline violated: *one place decides the geometry, so the drawn control and
the clickable control cannot drift.*

It has already bitten once, and the code says so: the hit bands used to be
three hand-written constants describing two rows, so **the third driver row
drew, said what it was doing, and could not be clicked** — *"a control that
looks live and is not, which is SPEC.md §47 rule 4's failure arriving where
rule 4 does not look because no predicate refuses anything."* Deriving the
bands from `DRV_MAX` fixed that instance and left the two-representations
shape intact.

### 5.3 The fix, scoped — as separate work

One resolver both sides go through, replacing the multiply/divide pair with a
walk that skips hidden entries:

```
cp_vis_row:   visible ordinal -> table index   (paint: row -> y)
cp_vis_hit:   the same walk, run against a y   (click: y -> row)
```

Applied to `cp_items` it retires "Display must be last" and lets that page be
positioned by what it *is* rather than by what hiding costs. Applied to
`cp_drv_*` it makes a hidden driver row possible anywhere, which is what the
original ask wanted. The cost is a walk over at most five entries **on a
click**, which is free at any speed this machine runs at, and a few dozen
bytes.

**Recommend doing it, and recommend not coupling it to this plan.** XMEM no
longer spends the constraint, so making the cheap plan wait on a refactor of
the Control Panel's hit-testing would be the one way to make it expensive
again. Two commits, in either order.

---

## 6. What was checked, and what it answered

### 6.1 Boot ordering is not a blocker — this was the expected wall

`kmain` calls `ovl_xm_a20` and `ovl_xm_init` **before `sched_init`**, and its
comment says why: *"this is the last moment at which no kernel ISR is
installed — the unreal-mode window inside `xm_init` runs with CR0.PE set and a
real-mode IVT."* The overlay loads at `drv_boot`, long after.

**The file itself is the refutation.** `xm_arm` masks NMI at port 0x70 and runs
the entire PE window inside one `pushf`/`cli` … `popf`, and it is **already
called at run time from `xm_ucopy`**, once per 1KB chunk, with `int 08h`
hooked and IRQ3/IRQ4 live. If a kernel ISR could break the transition, the
shipped transport has been broken since it was written. The early call is a
comfort, not a constraint.

Two neighbours checked with it:

- **The A20 probe's scratch stays free.** It writes one word at linear `0x0500`
  and one through the alias at `HMA_SEG:0510`, saving and restoring both under
  `cli`. `KERNEL_SEG` is 0x0060 so the kernel starts at 0x600, and SPEC.md
  §18.92's diskette parameter table is at `0000:0580`. The probe touches
  `0x500..0x501` and nothing else.
- **The GDT works from a heap segment.** `xm_arm` computes the `lgdt` base from
  DS at run time rather than baking `KERNEL_SEG` in, and the image is an
  ordinary heap claim well under 1MB, so the 24 base bits a 16-bit `lgdt` loads
  are still exact.

### 6.2 `[sch_lock]` has no API slot — and the kernel is where it belongs

Both transports raise the scheduler lock. A loaded image cannot:
`drivers/debug/debug.asm` records this in its own header as the reason it has
no `call` verb and no disk payload channel — *"and `[sch_lock]` has no API
slot. Adding one is kernel code."*

**The resolution needs no new slot and is already in the tree.** `dsk_xfer`
raises `[sch_lock]` *before* dispatching `DSV_BLK`, so a block driver is handed
a locked scheduler rather than taking one (`drivers/net/net.asm` says so
twice). The kernel's `xm_copy` cell does the same around its dispatch. The
overlay keeps its own per-chunk `cli` window, which protects against a
different thing — an ISR, not a task switch — and needs nothing.

The consequence is an improvement: **SPEC.md §41.8's context rule becomes the
kernel's to enforce rather than the loaded code's to document.**

### 6.3 Three things that need no new mechanism at all

- **`xm_owner` needs no slot.** `osapi_snd_fm`'s header states the rule: the
  requesting instance is stamped in the kernel because `snd_req_inst` reads
  kernel state, and loaded code that had to ask would need a slot of its own.
- **`xm_release_rec` stays a kernel routine** and becomes a dispatch through
  `xm_fptr`, or a `ret` when nothing is loaded. `instance.inc` keeps calling
  one name and never learns anything moved — the same argument SPEC.md §41.11
  made for keeping a `ret` in `kern_small` rather than three `%ifdef`s in a
  file that is not about extended memory.
- **`[cpu_feat]` needs no slot.** SPEC.md §41.11 established by grep that
  `CPU_F_A20` / `_HMA` / `_UNREAL` have no readers outside `cpudet.inc` and
  `xmem.inc`. The bits stop being written when nothing is loaded and
  `cpu_info`'s AH answers 0 — exactly what `kern_small` answers today, and the
  truth on a machine with no gate verified.

### 6.4 Three genuinely new obligations

- **A20 outlives the image.** Nothing in os8088 depends on 1MB wraparound, so
  leaving the line open is safe and closing it buys nothing. **Say so in the
  spec**, or a later reader implements the tidy-looking version and changes HMA
  aliasing under a BIOS that has opinions.
- **Unreal mode outlives it too.** FS and GS keep their 4GB limits after the
  image is freed. Harmless — a wider limit faults nothing — but SPEC.md §41.4's
  rule becomes *only `XMEM.DRV` writes FS or GS*, and it must now hold across a
  window in which the code is **not loaded** and the limits are still wide.
  Same grep, restated rather than inherited.
- **§4.3's no-bulk-claims invariant**, which is the one that will not announce
  itself.

### 6.5 The gate breaks, and it is the only one there is

`tests/xmcheck.py` reads `xm_tab` out of the running guest by kernel symbol and
diffs it across a package's teardown. It is the only thing exercising the
allocator and the force-free at all, and it exists because those three
`call xm_release_rec` sites were **silently absent for a year** after an
integration merge dropped them.

Moved out, `xm_tab` is at a heap segment known only at run time. The gate is
rewritable — read `xm_row`'s `DRVR_SEG` from the kernel's map, then the table
at that segment plus a fixed offset from the image's own map — but it is real
work on the one instrument standing between this feature and the exact failure
it has already suffered once. **Budget it as part of the conversion, not
after.**

---

## 7. What it costs

Measured where it could be measured, hand-counted where it could not.

**Removed from the kernel** (measured, §0): `.text` −1,034, `.bss` −124,
`.ovl` −386.

**Added back** (estimated), with the driver framing the first draft assumed
shown for comparison:

| item | as a driver | as an overlay |
|---|---|---|
| four slot bodies: stamp DH, raise `[sch_lock]`, dispatch, else §41.11's answers | ~150 `.text` | ~150 `.text` |
| the record and its filename | ~56 `.text` (`drv_tab` row + 2 strings) | ~30 `.text` (`xm_row` + 1 string) |
| publication | 4 `.text`, ~54 `.bss` (`DRVC_MAX` 5→6, `DSV_SIZE` 28→30) | ~14 `.bss` (one far pointer + the staged `XMV_*`) |
| dispatch body | ~20 `.text`, 1 `.bss` | ~14 `.text` |
| `drv_load_at` factoring | — | ~6 `.text` |
| `xm_sniff` | ~40 `.ovl` | ~40 `.ovl` |
| `SK_XMS` + `xm_release_rec` dispatches | ~30 `.text` | ~30 `.text` |
| hidden-row skips (§4.4) | ~30 `.text` | **0** |
| **add-back** | ~290 `.text`, ~55 `.bss`, ~40 `.ovl` | **~230 `.text`, ~14 `.bss`, ~40 `.ovl`** |
| **net** | ≈ −1,175 image | **≈ −1,240 image** |

So the overlay is ~65 bytes of image and ~40 of `.bss` better than the driver
framing — a modest win on its own. **The reason to take it is §4.4**: six
mechanisms that stop needing to exist, including the one that would have spent
a constraint the requester has explicitly asked to stop paying.

Expect **two of the three rungs**: 1,024 bytes of heap handed back to every
machine, and `KERN_CODE_MAX` going from 460 bytes left to about 1,270.

**Work, in commits:** SPEC.md first (§41 restructured, §52.11's overlay
generalised to "an overlay whose owner may be the kernel", §4.3's invariant
written down) — it is the binding contract and this changes an interface. Then
`drv_load_at`; then the image itself, which is mechanical (the code moves
nearly unchanged, `xm_arm` and the `cpu 386` islands included); then the four
slot bodies and the sniff; then the `xmcheck.py` rewrite, which is the long
pole. §5.3's Control Panel fix is separate work in either order.

---

## 8. Failing silent, and where the diagnosis belongs

**Accepted, and it needs no mechanism.** A machine that reports extended memory
and then cannot deliver it — A20 will not verify, the heap cannot fund the
image, the `.DRV` is missing from a hand-built floppy — says nothing. The image
is freed, the four slots answer tier 0's answers, and the Task Manager's
`XMS 0/0K` line is the only trace.

The requester's reasoning is the right one and worth keeping in the spec: **bad
memory is the system's fault, not the user's, and a boot-time notice makes it
the user's problem at every boot without giving them an action.** It is also
consistent with what SPEC.md §41.8 already tells every package — *branch on the
caps, never on the tier* — and with SPEC.md §47 rule 3, which refuses to
report a guess where the only honest test is doing the thing.

**The right future surface is an app, not a page.** A System Info tool in the
CheckIt idiom — CPU tier, adapters found, memory ladder, A20 state, the RTC
rung that answered, drivers loaded — is where "this machine has 4MB and os8088
can reach none of it" belongs: asked for, once, by somebody diagnosing. That is
a package, and it would read most of what it needs from slots that already
exist (`OSAPI_CPU_INFO`, `OSAPI_XMEM_CAPS`, `OSAPI_VIDEO`, `OSAPI_SYS_KB`).
Recorded here so the decision is findable rather than rediscovered; it is not
part of this work.

---

## 9. Acceptance

- `make kernsplit`: `kern_small` **byte-identical**. This touches `kern_big`
  only, and a `kern_small` size that moves is docs/KERN-SPLIT-PLAN.md §2's
  whole failure mode. (`kern_small` keeps its `%ifdef`: its floor machine is an
  8088, so it should carry neither `xm_row` nor the sniff.)
- The four cells stay at 0x0190..0x01A8 and **`wm_geom` at 0x01B0 has the same
  body**, which is what says the table did not shift (SPEC.md §41.11.1's own
  test).
- One `.o88` still serves both kernels; `make small` still does not rebuild the
  apps disks.
- **On an 8088**: all four slots answer tier 0's answers register-for-register
  — the SPEC.md §41.11.1 comparison re-run — the sniff sets nothing, and **no
  sector of `XMEM.DRV` is read**, checked with `os88marty.py`'s disk counters
  from outside the guest.
- **On a 286+ with XMS**: it loads with no `SYSTEM.CFG` asking,
  `OSAPI_XMEM_CAPS` reports the same KB the pre-conversion kernel reported on
  the same machine, and `tests/xmtest` + `tests/xmcheck.py` (rewritten per
  §6.5) still catch a missing force-free — verified the way that gate was
  verified the first time, by removing the teardown call and requiring the gate
  to FAIL.
- **`DRV_MAX` is still 4 and `DRVC_MAX` still 5.** The Drivers page shows the
  same four rows it shows today, and a `SYSTEM.CFG` written before the
  conversion restores every tick unchanged — which is the single clearest
  statement that this did not touch the driver registry.
- `mem_sum_kb` counts the image under System (§4.3), and the image holds no
  claim of its own.
- Once it is out, `kern_big` carries **no** `int 15h` outside the sniff, no
  port-0x92 access and no `mov cr0` — SPEC.md §41.11.1's count run against the
  build that still has the feature.
- A settled desktop on a cycle-accurate 5150 with the real IBM Oct-82 BIOS: CGA
  at 60.0% lit, and the Task Manager opens and reads `XMS 0/0K`.

---

## 10. What the build found that the investigation did not

Three things, and the first two are the reason this section exists.

**1. A `%endif` 88 lines too low took the middle out of `drv_load` — in
`kern_small` — and it assembled without a word.** Guarding `drv_load_at`
behind `%ifdef KERN_BIG` is correct (§5's leak table, row 1: *a hook left
outside its `%ifdef`*), but the guard closed after the `DRVC_OVL` test rather
than after `drv_load_row:`, so the small build lost the mount, `drv_find`, the
claim, the read and `drv_check`. It assembled because **every label the
guarded region defined was also inside it**, so nothing was left dangling.

What caught it was `make kernsplit` and nothing else: `kern_small`'s `.text`
came out **92 bytes smaller** than HEAD in a commit that is entirely about
`kern_big`, which is docs/KERN-SPLIT-PLAN.md §7's stated failure mode arriving
exactly as described. Three earlier measurements had been *dismissed* as stale
baselines before a clean build settled it — so the second lesson is the
cheaper one: **when the split reporter and your expectation disagree, do the
two clean builds before you explain the number away.**

**2. A register the callee documents as clobbered, believed across a far
call — and it worked on the first machine tested.** `xm_boot` handed
`drv_call` the row in BX and then read `[bx+DRVR_SEG]` after it returned;
`xm_attach` spends BX on the KB figure and says so in its own header. The
staged service table was therefore ten bytes of whatever the heap held.

Both halves of how it hid are worth keeping. **The visible outputs were all
correct** — `xm_kb` right, `cpu_feat` right, the image loaded and freed
properly — because AX and DL came back through the stack; only the table was
wrong, and a table of near offsets that are not near offsets looks like
nothing until something calls through it. And **the refusal path had the same
bug and passed anyway**: a tier-0 refusal returns from `xm_attach` before
anything spends BX, so `drv_release` got a valid row by luck. The machine that
would have exposed it — a 386 whose A20 will not verify — is §41.12.1's one
predicted false positive, i.e. the rarest path there is.

**3. `int 15h` AH=88h on a period 5150 answers "none", which is the right
answer and not a test.** Forcing the tier check out of the sniff changed
nothing on MartyPC, because a real XT BIOS correctly reports no extended
memory. Exercising the load on that machine needed the sniff stubbed to set
`DRVR_WANT` unconditionally — and that run is worth keeping as a recipe,
because it is the only way to drive §41.12.1's false-positive path on the
target hardware: **7 extra sectors read, the overlay refused at attach, the
image freed, `xm_kb` still 0, the desktop unchanged.**

### 10.1 How it was verified

| check | result |
|---|---|
| `make kernsplit` | `kern_small` **byte-identical** to HEAD, md5 `368160ab…` |
| 8088, cycle-accurate (`os8088_5150_cga_gla`) | `cpu_tier` 0, `cpu_feat` 0, `xm_kb` 0, `DRVR_WANT` 0, `DRVR_SEG` 0 — **not one sector of `XMEM.DRV` read**, desktop settles in 2.06 s |
| 8088 with the sniff forced | loads, refuses at attach, image freed, `xm_kb` 0, +7 sectors, desktop unchanged |
| 386 (QEMU — §41.7's legitimate use) | `cpu_tier` 2, `cpu_feat` **0x7** (A20 verified, HMA claimed, unreal armed), `xm_kb` 64,448, `DRVR_SEG` 0x9dc0, `DRVR_KB` 2, service table five plausible offsets inside a 1,568-byte image |
| `tests/xmtest` + `tests/xmcheck.py` | 3 instance-owned blocks live with the window open, **0 after the close** — so the whole chain runs: sniff → load → attach → staged table → slot dispatch → allocator → teardown dispatch |
| the same gate, `call xm_release_rec` commented out of `instance.inc` | **FAIL, 3 blocks outlived their instance** — which is what makes the green run mean something |

`tests/xmcheck.py` needed the rewrite §6.5 budgeted for: `xm_tab` is at a heap
segment now, so it reads `xm_row`'s `DRVR_SEG` out of the guest and adds the
offset from the **overlay's own** nasm map — assembled from a copy and
required to be byte-identical to the `build/xmem.bin` that shipped, which is
`os88sym.py`'s discipline applied to the other image.

---

## 11. The constraint itself, fixed (SPEC.md §31.10.1)

§5.3 scoped this and recommended not coupling it; it was taken next, on its
own, and the shape it needed is **not** the one §5.3 guessed.

**§5.3 was wrong about where the problem was.** It read `cp_drv_click`'s
divide against `cp_drv_paint`'s walk as two representations of one mapping —
the drift shape. They are not: both derive from the same two constants
(`CP_DR0Y`, `CP_DROWH`), so they are exact inverses and *cannot* disagree
numerically. What they cannot express is a hole, which is a different
complaint. And `cp_pick` — the item list's hit-test, the one that actually
matters — was already a subtract-loop mirroring `cp_list`'s add-loop, so
there was no divide to remove there at all.

**The real constraint was one line of arithmetic**: `cp_entry` turned a row
index into a record with `index << 3`, an identity mapping. That is what made
a hidden row renumber everything below it, and it is why the hidden row had
to be last.

**The fix separates two index spaces that were one number.** A row has a
RECORD index (its position in `cp_items`, stable) and a VISIBLE ORDINAL (its
position in the list on screen). `[cp_sel]` and the six routines that reach
pages name records; `cp_list` and `cp_pick` work in ordinals; **`cp_v2r` is
the one place they meet.** `[cp_hide]` — one byte, one bit per static record
— carries what `[cp_nst]` could not: a count can only make the list shorter,
never say *which* row is missing, and that gap was the whole constraint. The
static/driver boundary went back to `CP_ITEMS`, because it is a question
about the table rather than about the screen.

`CP_ITIME` and `CP_IDRV` still work unchanged at their five call sites, which
is the test that the split is the right way round: those name pages, and a
page does not move because another one is not being shown.

**Verified twice, and the second one is the point.** The same scripted
session on a cycle-accurate 5150/CGA where Display IS hidden — chip menu →
Control Panel, then Drivers, Sound and back to Scheduler — gave **four
framebuffer captures identical before and after**, digest for digest. Then
`[cp_hide]` was seeded to hide **Date/Time, record 1, a MIDDLE row**, on top
of Display: the list drew Scheduler / Drivers / Sound contiguously and
clicking the second visible row selected **Drivers** and painted the Drivers
page. That is the case the old mapping could not express — it would have
selected Date/Time while the list drew "Drivers" over it.

**Cost, and who pays it.** `.text` +6, `.cold` +84 — and **`kern_small` pays
it too**, because `ctrl.inc` and `vidsel.inc` are in both builds. That is not
docs/KERN-SPLIT-PLAN.md §2's leak: this is shared code improved for both
products, not a `kern_big` feature charged to the floor machine. No rung
moved in either build. `kern_small` boots and opens the panel.

**What was deliberately left alone: the Drivers page.** It has no hidden rows
and no way to acquire one, its bands are uniform, and its two sides share
their constants — so adding skip machinery there would be speculative
generality for a hole that does not exist. If a driver row ever needs hiding,
`cp_v2r`'s shape is the answer and `drv_tab` would want a bitmap of its own.
The third claimant, §51.2.1's settings bitmap, is untouched and should stay:
append-only is the correct discipline for a file that survives across
versions, and no index-space split helps with it.
