# `kern_dos` — giving a DOS program the whole machine

**Status: PLAN. Nothing is built.** docs/plans/DOS-EXEC-PLAN.md §14 describes
this phase in a few sentences, which was right while it was far away. This is
that phase costed.

**The ask, in one line:** the Memory page's check box becomes a **radio** —
*Keep all* / *Dump the disk cache* / *Dump the whole OS* — and the third arm
takes os8088 out of memory entirely so a DOS program gets what it would get
under DOS.

---

## 1. The target is 600 KB, and that makes this a BUDGET

The requester's number: **600 KB free for the program**, because the RAM hogs
that motivate the work need 580 and the margin should hold a small disk cache.

That is not a goal, it is a constraint, and it decides every other question in
this document. The arithmetic on a 640 KB machine:

```
  655,360   the machine
   -1,536   the IVT and the BDA (0000:0000 .. 0000:0600)
 -614,400   the program's 600 KB
  =======
   39,424   EVERYTHING ELSE
```

**~39 KB** for the whole of `kern_dos`: its code, its sector buffers, its
stack, the PSP and environment, and whatever cache it keeps. For scale, that
is about what IBM DOS 3.30 costs, which is the right company to be in and not
a coincidence — we are building the same thing.

### 1.1 Where the 640 KB goes today, MEASURED

Read off a running `os8088_5150_herc_sb_gla` at the moment the arena is
claimed (`tests/dosarena.py`'s probe, this session):

| | KB |
|---|---:|
| the DOS arena the program actually gets | **449** |
| the DOS package's own region | 42 |
| the directory read-ahead window (`dirw`) | 32 |
| `KERN_SIZE` — the kernel, its modules and its low area | 112 |
| the IVT, the BDA and rounding | ~2 |
| | **637 of 640** |

So the three radio arms are worth, roughly:

| arm | what it gives | how |
|---|---:|---|
| Keep all | **449 KB** | today's default |
| Dump the disk cache | **~481 KB** | the 32 KB `dirw`, which is `[dos_keepc]` unticked today |
| Dump the whole OS | **~603 KB** | everything above, less `kern_dos` itself |

**603 is the estimate to beat and it has no cache in it.** A 16 KB read-ahead
would put it at ~587 — still over the 580 the hogs want, but the margin is
four kilobytes and that is not a margin. §6.6 proposes the way out.

> **WAVE 1 HAS MEASURED THEM** —
> docs/reports/KERN-DOS-BUDGET-2026-09-13.md. Arms 1 and 2 stand: **449 KB and
> 481 KB off one boot**, so the 32 KB between them is a difference rather than
> two readings, and the table above is confirmed. **Arm 3 is worse than 603**:
> the floor is 35.5 KB measured against §6's ~30.5 estimated, so the honest
> arm-3 figure before §6.1's levers is ~**603 KB with no cache and no shim
> budget at all** — 3.0 KB of the 38.5 is what is left. Two of the five levers
> are priced there and are worth 4.1 KB together.

---

## 2. The finding that changes the shape: §87 already built the hard part

The obvious hard problem is **overwriting yourself**. `kern_dos` has to land in
low memory, which is where the kernel loading it is standing.

**§87.5 solves this already, and better than the obvious answer.** The
hibernate resume:

1. walks the image's FAT chain into a list of **extents** — absolute LBA and
   sector count, coalesced, six bytes each — while the file layer is still
   alive;
2. copies a **~450-byte stub**, its parameters and that extent list into the
   **text framebuffer** (`B800:0000`, or `B000:0000` on a Hercules);
3. tears the machine down and jumps into the stub, which reads the extents
   with `int 13h` and jumps to the restored entry point.

Video RAM is the one memory on the machine that a conventional-memory image
does not cover. A top-of-RAM relocator — the obvious design, and what
`boot/boot.asm` stage 1 does — would have to be *excluded* from the image;
this needs no exclusion at all.

**So the handoff is that stub with a different payload**, and the return is
that stub with its original payload. Both directions are one mechanism that
exists, ships, and has a gate (§87.8).

### 2.1 What that does to the restore question

The requester asked for the restore to be costed rather than assumed away —
*"waiting 4-5 seconds to get to desktop again and THEN a few more for the
restore"* against *"a few seconds for the restore"*.

**The direct restore is nearly free**, because `kern_dos` does not have to
find `HIBERNAT.IMG` — the OS walks its chain into an extent list *before* it
tears itself down and hands `kern_dos` the list. `kern_dos` needs no FAT, no
directory and no file layer for the return path: it needs the stub and the
list, both of which it is given.

So the plan takes the direct restore. `int 19h` stays as the **fallback**, and
it is not a wasted path — §9's no-hard-disk arm ends in exactly that, and a
`kern_dos` that cannot make sense of what it was handed must have somewhere to
go.

### 2.2 What the round trip costs, from §87.7

> *the image is 1,280 sectors on a 640 KB machine, and both the write and the
> read go out in track-sized runs, so it is ~80 `int 13h` calls each way
> against an XT hard disk — seconds, not minutes.*

**~160 `int 13h` calls for a launch and a return.** The image is the whole of
conventional memory; there is no extent-skipping of free heap. That is a fixed
cost per launch and it is the honest price of the third arm, alongside the
losses in §10.

> **MEASURED, and the answer is ~4 SECONDS** —
> docs/reports/KERN-DOS-BUDGET-2026-09-13.md §3. The owner hibernated on
> **three machines including the real 5150 with a real ST-225 in it**, all at
> 640 KB with no XMS: **~2 s to write and ~2 s to resume** on every one, and
> the 4.77 MHz and 10 MHz machines agree — a figure the CPU speed does not
> move. So §2.1's *"a few seconds for the
> restore"* is exactly what it is, the judgement §2.1 already made stands, and
> arm 3's handoff is cheap.
>
> **The first version of that report said 43.4 seconds and it was the wrong
> instrument, not a wrong reading.** It was taken on `os8088_xt_hdd`, whose
> transport is **XT-IDE** — and MartyPC's hard disk has *no timing model at
> all*: the mechanical model this tree wrote and field-checked
> (`tools/martypc/patches/04-floppy-disk-timing.patch`, PERFORMANCE.md Part 9
> Set 37) is the FLOPPY's, and the ATA device carries one 200 ms reset
> constant and nothing per sector. So 92% of those seconds are the 8088
> grinding through the option ROM's byte-at-a-time PIO at 25 KB/s, where the
> field machine's **ST-225 on an ST-11M** does ~320. Our own batching was
> checked on the way and is fine — track-capped, ~50 `int 13h` calls each way,
> exactly what §87.7 claims.
>
> **The rule, which is the tree's and not this plan's: a hard-disk TIMING off
> MartyPC is not quotable.** Counts and call shapes are exact as ever;
> milliseconds are not. docs/TESTING.md carries it.

---

## 3. The second finding: `dos_be_*` is the whole port

The DOS box reaches the file system through **twenty `dos_k_*` back-end
targets** behind `dos_be_*` doors (§96.4.1). That layer exists for a reason
that has nothing to do with this plan — a kernel file slot must run on the UI
task's stack, because `dsk_secbuf` is `.lowbss` and reached through SS
(§96.4.1.1) — but it is exactly the seam a port needs.

Measured this session: every `OSAPI_FILE_*` / `OSAPI_VOL_*` / `OSAPI_FIND*`
call in `apps/dos/` is inside a `dos_k_*` target, except for eight in the
launch and shortcut paths (`dos_load`, `dos_lnk_*`, `dos_sav_go`,
`dos_path_make`, `dos_trace_dump`). Of those, only **`dos_load`** — reading the
program image — is needed under `kern_dos`; the rest are window-side.

> **W2 CHECKED IT PROPERLY AND FOUND THREE** (SPEC.md 96.4.2). The paragraph
> above was a grep over file names and prefixes; walking the call graph from
> the interrupt entries instead found `dos_walk_at` → `OSAPI_FILE_HERE`
> (reached from `dos_int21` via `dos_fh_enter`) and `dos_drv_count` /
> `dos_drv_sel` → `OSAPI_VOL_KIND` (both straight off `dos_int21`). **All
> three were outside for the same reason** — the slot does no disk I/O, so
> §96.4.1's stated reason does not bind and the direct call looks right. They
> are `DBE_HERE` and `DBE_VKIND` now, **22 doors**, +24 bytes of package image
> and no kernel byte.
>
> **`dos_load` turned out NOT to be an exception**: it is not in the outside
> list at all, so the port is *twenty-two doors and nothing else*.
>
> Two things the same walk establishes about §4.1.2's split, which is the
> other thing W2 owed. The reachable core touches **none** of `os88ui.inc`,
> `os88line.inc`, `os88parts.inc` or the socket layer — zero sites. It does
> reach `dosc.inc`'s console in three procs, and **all three are the
> outside-the-bracket arm**: `dos_tty` already branches on `[dos_inbr]` and
> takes the ROM's `int 10h` teletype inside the bracket, which is §6.1 lever
> 5's premise confirmed rather than assumed. And the whole non-file `OSAPI_*`
> surface the core can reach is **two slots**, `OSAPI_DRV_CALL` and
> `OSAPI_MOUSE`, registered in `tests/dosseam.txt` so it cannot grow quietly.

> **The port is a second implementation of twenty doors plus `dos_load`, and
> nothing above them changes.** `dos_fh_*`, the INT 21h dispatch, the PSP, the
> FCB layer, `AH=4Bh`, the memory chain and the mouse translation are all
> untouched source.

That is what makes this phase tractable, and it is the thing to verify FIRST
(§11 wave 2) because the whole plan rests on it.

---

## 4. The shape: neither option one nor option two

The two shapes on the table were **(one)** a second full kernel build,
`KERNDOS.SYS` on the system disk, and **(two)** a minimal kernel plus the DOS
half as a loadable part of `DOS.O88`.

Neither duplicates *source* — this tree already builds three kernels from one
source (`make small`, `make emu`) and a fourth arm is ordinary. What they
differ in is **disk bytes on the system floppy**, which is the disk about to
come under pressure from the system-app work, and **how much machinery has to
be invented**.

### 4.1 The proposal: `kern_dos` is a PART of `DOS.O88`

> **THIS SECTION STANDS, AND ITS TABLE IS CORRECTED — MEASURED, W5b.**
> docs/reports/KERN-DOS-PART-COST-2026-09-14.md is the measurement. The
> `system-disk bytes: 0` row is wrong — `DOS.O88` is ON the 360KB system disk
> (the Makefile's `SYSROOT`, in `APPS/`), so a compressed part costs **43 of
> the 53 clusters that disk has left**. A `KERNDOS.SYS` file costs **38**,
> because `tools/os88pkg.py` refuses whole-file compression on a parted
> package and `DOS.O88` gives back its own 5,425 bytes.
>
> **Five clusters is a wash and PORTABILITY decides it, so the part wins.**
> One file carries the whole function from disk A to disk B; a sidecar is what
> docs/plans/completed/O88-MULTISEG-PLAN.md wave 6 removed from `apps/c64`
> precisely because a file copy could separate it from its program. An
> individual part still compresses (`OP_COMP`) — only the enclosing package
> stops doing so.
>
> **What the measurement really found is DUPLICATION**, which is §4.1.3 below.
>
> **AND THE 43 IS 17 NOW, ON THE SHIPPED DISK — the whole argument above was
> settled by making the package a different shape rather than by choosing
> between these two.** §4.1.3's duplication was the real finding and W9 spent
> it: the core is assembled once and shipped once (SPEC.md 96.44.5), the
> package is a 2,092-byte raw loader in front of three compressed parts, and
> `DOS.O88` is **44,337 bytes against the 57,272** this costing was taken on.
> `$(SYSROOT)` points at it, so the Memory page's third arm is live on every
> shipped system disk, at **17 of the 360KB disk's 50 free clusters and +750
> ms a launch** — SPEC.md 96.40.3 carries both measurements and why the
> earlier refusal was right about the package it was taken against. The gate
> disks `build/kdos360.img` and `build/kdos144.img` are gone with it: they
> built byte-identical to the shipped images the moment the variable flipped.

**One assembly root, `kerndos/kerndos.asm`**, which `%include`s the kernel's
disk layer and the DOS core from where they already live — and ships as
**part 1 of `DOS.O88`** (§20.12), not as a file on the system disk.

| | one | two | this |
|---|---|---|---|
| system-disk bytes | ~22 KB packed | ~13 KB packed | **0** |
| new load mechanism | a loader | a loader + a mini-ABI | **none — `OP_ASSET` already** |
| DOS core shipped twice | no | no | no |
| versions with the box | no | half | **yes** |

`apps/c64` is the worked example: 20,480 bytes of KERNAL, BASIC and CHARGEN as
part 0 of `C64.O88`, *"a sidecar a file copy could separate from the program"*
made part of it. A `kern_dos` image is the same thing and the same size class.

#### 4.1.1 The part costs NOTHING during an in-OS run, and nothing at the handoff either

**The requirement:** arms 1 and 2 are an ordinary windowed DOS box, and the
`kern_dos` part must not be in RAM while one runs. §20.12 has lazy parts for
exactly that, so the floor is *loaded only on the arm-3 handoff*.

**But it is better than that, and for free.** §2's handoff walks `HIBERNAT.IMG`
into an extent list and lets the stub read it with `int 13h` — and the part is
a byte range of `DOS.O88`, which is a file on a volume, so **the same walk
turns it into extents too**. The stub reads `kern_dos` straight into low memory
off the disk.

So the part is **never loaded as a part at all**: not during a windowed run,
not during the handoff, and not into a scratch claim that has to be found on a
heap the launch is about to give away. What the box holds is the part's file
offset and length — which §20.12 puts in the *image*, so reading them costs no
disk at all.

#### 4.1.2 The UI half does not come along

The part is built from the **core only** — a second assembly of the shared
source with the window, the menu, the pages, the console, the shortcut writer
and the file dialogs excluded. `apps/dos/dos.asm` is one file today and has to
be split so the core is `%include`-able; that is W2's real work and it is the
same work either of the original options needed.

**THIS PARAGRAPH IS WRONG AND W4 IS WHY** (SPEC.md 96.38). Nothing had to be
split. `apps/dos/dos.asm` is `%include`d **whole and unedited** under
`kerndos/kdos.asm` and produced exactly **one** name collision in 13,000
lines — `DVOL_MAX`, which the kernel's own `assoc.inc` defines first — plus
three `%ifndef KD_BACKEND` gates, every one of them around the **package
container**: `OS88_HEADER`/`OS88_ICON16`/`OS88_ASSOC16`, `OS88_BSS`, and the
`dos_k_*` block the second back end replaces. Not one gate is around a line of
DOS logic, and the window, the menu, the pages, the console and the file
dialogs all assemble under the second root without complaint.

Two reasons, and the second is the surprise:

1. **The three container macros assert their own file offsets** (0, 32, 96),
   so they are the only construct in the file that *cannot* assemble where
   the image does not start at offset 0. Everything else is position
   independent by the near model's own rules.
2. **The UI half reaches the kernel through `OSAPI_*` cells, which are far
   address literals.** An unreached `call KERNEL_SEG:0x0310` costs nothing but
   its bytes — it is dead code in this root, not a link error — so the cost of
   bringing the window along is **image size and nothing else**. That turns
   "split the file" from a prerequisite into §6.1's kind of question: a size
   lever to be measured against the 39 KB budget, taken or not on its own
   arithmetic, at a wave that has a budget to spend.

What the wave DID find, and neither is a split: `apps/os88con.inc` is
unreachable in this root (the program owns the machine, so `[dos_inbr]` is 1
for ever and `dos_tty`'s console arm is dead), and the entry and exit paths —
21 procs from `dos_save_machine` through `dos_terminate` — reach **no
`OSAPI_*` at all**, which is why they port by being included rather than by
being ported.

**AND W5b PUT A PRICE ON LEAVING THEM IN**, which W4 could not: unreached
bytes cost image size and nothing else, and on a disk image size is the whole
cost. The measured spans are 5,675 for the window half, 2,090 for the console
library, 1,665 for `dosc.inc`'s prompt, 3,164 for `dosnet.inc`'s packet driver
(§10 says arm 3 has none) and 1,997 for `.ovlw` + `.modf` — **14,622 bytes**,
which takes `kern_dos` from 46,407 to about 31,800 and its packed form under
the room a 360KB system disk has. `apps/dos/dosh.inc` STAYS: it reads like
`dosc.inc`'s pair and is not one — it is the **built-in commands** (SPEC.md
96.30), the `COMMAND.COM` that is not a file, which `AH=4Bh` reaches and which
Microsoft C's `system()` is.

**Why it beats option two specifically:** two's economy comes from loading the
DOS half separately, which needs a mini-ABI between two halves that are built
together anyway — and a mini-ABI between two things one team maintains is the
kind of interface that rots silently. Here the two halves are linked at
assembly time and the "load mechanism" is `op_load` plus the §2 stub.

**Why it beats option one:** the system disk is at 255 of 354 clusters at 360
KB, and the next development focus puts more system apps on it.

#### 4.1.3 The DOS core would be in `DOS.O88` TWICE, and it need not be

**MEASURED** (docs/reports/KERN-DOS-PART-COST-2026-09-14.md §4): the core —
`dos_int21` and everything under it, the PSP, the handle layer, the FCBs, the
MCB chain, `AH=4Bh` and the built-in commands — is **12,812 bytes of
`DOS.O88`'s own 31,868-byte image**, and every byte of it is inside the
`kern_dos` part as well. Nothing in §4.1 costed that, because §4.1 was about
where the part goes rather than what is in it.

Extracting the core to a **third part both halves share** is worth about **12
KB of every system disk, for ever**, on top of §4.1.2's 14,622:

| | clusters on a 360KB system disk |
|---|---:|
| the part as W5a builds it, the box left in | +43 |
| the box cut from the part | +32 |
| …and the core extracted to its own part | **+20** |

**THE SEAM IS ONE-DIRECTIONAL AND THAT IS WHY IT IS WORTH DOING.** Box → core
is 46 transfers at 33 entry points, the busiest being five call sites and
none in a per-character path. **Core → box is ZERO** — not luck but §3: the
one edge that exists is `dos_be_go`'s `jmp word [dos_betgt]`, the door table
this plan already built and `kerndos/kdback.inc` already re-implements.

Outside itself the core reaches **four `OSAPI_*` slots and six library calls,
in three procs**. Six of the seven are the windowed arm W4 showed `kern_dos`
never takes (`[dos_inbr]` is 1 for ever, SPEC.md 96.38). The two that are not
are `OSAPI_MEM_CLAIM` and `OSAPI_MEM_FREE`, which SPEC.md 20.12's parts rule 2
forbids a part outright — and answers in the same breath: *the primary claims
and passes a segment down*. So the whole of the new work is **two more doors,
three more, or three procs moved**.

The state seam is **46 of 251 bss cells**, twelve of which are the `DOSTRACE`
build, the packet driver and the Memory page's radio. **The ~34 that remain
are the launch block** — `kerndos/kdlaunch.inc` already marshals seven of them
across a segment boundary and is the shape the rest take.

**The shape**: one assembly, `OP_SEG | OP_COMP`, far-called with its own bss,
in BOTH hosts — the box reaching it through `op_load`/`op_seg`, `kern_dos`'s
stub reading the same part's extents into a segment of its own and far-calling
the same 33 entry points. **One ABI, because it is the same ABI.**
`apps/skies/csload.asm` is the worked example: a package whose part 0 is a
whole `.o88` image, compressed, far-called, with its own bss and a handoff
block at the head of it.

**It is NOT a prerequisite for W5c** and should not be made one: the handoff
does not care how many parts it walks, and doing the refactor first would put
an unbuilt seam under an unbuilt stub. It is a wave of its own — W9 below.

##### 4.1.3.1 The shape, and the join is NEAR

`DOS.O88` becomes four pieces, which is the owner's design and costs **+19
clusters of a 360KB system disk against today's 26** — better than every other
shape measured:

| | what | how |
|---|---|---|
| **image** | the parts loader, ~2 KB, dropped once it has loaded | `OSAPI_PKG_REHOME` |
| **part 0** | the UI — becomes the main image when the loader rehomes to it | `OP_SEG, OP_COMP` |
| **part 1** | the INT 21h core | `OP_SEG, OP_COMP` |
| **part 2** | `kern_dos` — the FAT, the mouse, the kernel bits | `OP_SEG, OP_COMP` |

**Part 1 joins to EITHER part 0 or part 2 and never both**, because the two
hosts are alternatives: one is the windowed box and the other is the machine
after the handoff. Every mechanism it needs is built — `OSAPI_PKG_REHOME` is
six bytes on an ordinary launch, and `apps/skies/csload.asm`'s loader measures
**2,000 bytes**, so the ~2 KB estimate is exact.

**AND THE JOIN CAN BE NEAR**, which is the finding that makes the whole thing
cheap. A near join needs the core at the same offset in both hosts, so each
reserves the range below it — and the two hosts measure **19,556 and 18,959
bytes**, within 600 of each other. A 512-aligned `CORE_ORG` of 19,968 leaves
holes of **412 and 1,009 bytes**; part 0's is zero-run padding that compresses
away and part 2 needs none at all, because the stub places it rather than
carving it.

With a near join, every obstacle §4.1.3 listed dissolves: the 46 transfers
stay near, there is no second `DBSS` chain (`os88_image_end` is the same
offset in both), rule 2's ES fence never applies because it is the host's own
segment, and the six library calls become six words of vector the host fills.
What is left is **a 33-entry jump table at `CORE_ORG` (99 bytes)**, because
part 0 cannot know the core's internal addresses at assembly time, and
**`CORE_ORG` as a budget with two claimants** — a `KERN_BUDGET`-shaped ledger,
because today's 600-byte margin is luck and will not stay lucky.

###### 4.1.3.1.1 …and the 600-byte margin is 4,628 now, so the core goes LOW

**RE-MEASURED at W9c, and the section above no longer describes this tree.**
Its near join rests on one sentence — *"the two hosts measure 19,556 and
18,959 bytes, within 600 of each other"* — and W8 spent the next wave cutting
**14,622 bytes out of one of them**. The two are not within 600 any more and
never will be again: `kern_dos` carries `disk.inc` and `diskw.inc` (11,935)
and no window, the box carries the window half (12,033) and no disk layer, so
the difference is *structural* rather than a coincidence that drifted.

MEASURED on this tree (`tools/os88doscost.py` for the core, symbol spans for
the rest):

| | image | less the core | |
|---|---:|---:|---|
| the box | 33,619 | **18,389** | window half + the shell + the container |
| `kern_dos` | 28,991 | **13,761** | the disk layer + the shim + the refusal wall |
| the core | — | 15,230 | +3,846 of bss, identical in both |
| | | **4,628** | the margin §4.1.3.1 costed at 600 |

**Put the core ABOVE the hosts, as §4.1.3.1 says, and that 4,628 lands on
`kern_dos` as a HOLE**: `CORE_ORG` has to clear the larger host, so it is
18,432 and `kern_dos` has 4,671 bytes of address space between its last byte
and the core's first. Its own `.bss` is 5,523 and would cover it — but the
core's bss has to be at ONE offset in both hosts (§96.44.2), so the bss that
could fill the hole is the very thing that may not move into it. **It is
~4.7 KB off `KD_IMG_KB`, which is ~4.7 KB off the DOS program**, in a plan
whose entire purpose is that quantity.

**PUT THE CORE LOW INSTEAD and the hole disappears.** Each host's own code
sits ABOVE a fixed-size core, so there is nothing to pad: the core is at
`CORE_ORG`, the hosts start at `CORE_ORG + CORE_MAX`, and what each host
costs is only what it is.

| | what is below `CORE_ORG` | wasted |
|---|---|---:|
| `kern_dos` | the 8-byte header and §96.40.2's refusal wall, which END AT 0x05A8 and cannot move — `apps/os88api.inc` owns those offsets | ~8 |
| the box | the package header, 0x70, which `OSAPI_PKG_REHOME` step 8 dispatches through and which therefore cannot move either | ~1,344 |

So `CORE_ORG = 0x05B0` and the trade is **1,344 bytes of the box's heap
region against 4,671 of `kern_dos`'s arena** — and those are not the same
kind of byte. The box's region is heap on a machine with 437 KB free; the
arena is the 586 KB this whole plan is a budget for.

**What it costs instead is a budget with ONE claimant**, which is the other
half of why it is better. §4.1.3.1's `CORE_ORG` is a ledger two things push
on from opposite sides — grow either host and the other pays — and it says
so (*"today's 600-byte margin is luck and will not stay lucky"*), which it
was not. `CORE_MAX` is a `KERN_BUDGET`-shaped rung the CORE alone spends,
the hosts read it, and slack is one number in one place: at 15,360 the core
has 130 bytes of it.

The box's 1,344 is not even certain to be waste — it is exactly the kind of
room the box's own header, icon and association table already want — but it
is costed as waste here so the decision does not rest on finding a use.

One thing to keep straight: **part 2 is not loaded by the parts loader.**
§4.1.1 is why — the heap is being given away, so there is nowhere to load it
to. The handoff walks its bytes into extents while the file layer is alive and
the STUB reads them, two runs now: part 2 to `KD_SEG:0000` and part 1 to
`KD_SEG:CORE_ORG`. Same loop, one more extent list.

### 4.2 What "kernel" means here, and what it does NOT import

Per the requester: *the thing that sits there running DOS programs*, and
nothing else. `kern_dos` has **no boot sequence** (it is jumped into, not
booted), **no task table or scheduler**, **no API table**, **no window
manager**, **no drawing layer**, **no menu**, **no event loop**, **no driver
layer** and **no on-demand module mechanism**. It is a resident INT 21h with a
FAT reader under it.

This is why it is a new assembly root and not a fourth `%ifdef` arm of
`kernel.asm`: gating 80% of a file out is a permanent tax on the *shipping*
kernel's readability. The included files (`disk.inc`, `diskw.inc`) keep their
`%ifdef`s; `kernel.asm` gains none.

---

## 5. The shim, and the honest risk in it

`disk.inc` and `diskw.inc` do not stand alone. They take `[sch_lock]`, drive
the `fpg_*` progress widget, sit on `.lowbss` buffers reached through SS, and
call into the volume table and the driver layer.

**The shim is the work nobody can estimate from outside**, and it is §11 wave
3's first job: assemble the two files under a root that is not `kernel.asm`
and satisfy what they name. The precedent for measuring it is
docs/plans/completed/KERN-SMALL-MODULE-SPLIT.md §8, which walked `mod_need`'s
transitive cone and found **155 symbols in 7 files** — and whose own lesson was
that the first call-graph pass *undercounted*, because it could not see
`call far COLD_SEG:label`.

Expected shape of the shim, ESTIMATED:

- `sch_lock` / `sch_unlock` → `ret` (there is one task)
- `fpg_*` (the progress widget) → `ret`
- `cur_*` (the pointer) → `ret`, or the mouse's own
- the volume table → kept, but populated from what the OS **hands over**
  rather than re-probed (§7 step 3), which also skips §18.97's floppy probe
- `mem_*` → a bump allocator over the region above `kern_dos`

**If the shim comes out large, that is the finding that reopens §4.** A shim
bigger than a purpose-written FAT reader means the reuse is not paying, and
the answer is a small reader rather than a big shim. Wave 3 is allowed to
return that answer.

> **IT IS 92 BYTES AND §4 DOES NOT REOPEN** (SPEC.md 96.37). 58 of `.text`,
> 25 of `.bss`, 9 of `.cold`, against **11,935 bytes** of kernel disk code —
> 0.7%, and the answer is not close. The three files name **81** external
> symbols: 32 constants lifted verbatim, 6 macros about a machine `kern_dos`
> has not got, 29 stubs and refusals and strings, and **14 that are real
> work** — the bump allocator (51 bytes, §6.2's purgeable cache in miniature)
> and `kernel.asm`'s epilogue ladder.
>
> **Every item of §5's estimate was right**: `sch_lock` is a byte, `fpg_*` is
> a `ret`, the cursor is gone with `mouse.inc`, and the allocator is a bump.
> The one thing it did not predict is what the reuse BRINGS: `.ovlw` and
> `.modf` come along at **1,987 bytes** of boot-overlay and FORMAT-module
> code a machine with neither has no use for (96.37.2). Dead weight in a
> section, not a dependency, and a later wave's to gate out.
>
> **It mounts and reads**, which is the claim worth having: `tests/kerndos.py`
> boots it, reads a file off a FAT12 floppy and checksums it against
> `tools/os88fat.py` on the host. It went red four ways getting there and
> SPEC.md 96.37.1 has them, of which two are worth the reading — a near `ret`
> under a FAR call, and the on-disk record offsets read out of a synthesized
> entry.

---

## 6. What goes in `kern_dos`, against the 39 KB

Sizes from docs/plans/KERN-SMALL-CUT-PLAN.md §1.2's attribution (heap-bearing
sections, `kern_small`) — **an upper bound**, since that table is the whole of
each file and `kern_dos` wants part of it.

| | bytes | note |
|---|---:|---|
| `disk.inc` — volumes, mount, FAT read | 6,816 | less mount UI, less multi-volume |
| `diskw.inc` — the FAT write path | 5,077 | less the batch/undo machinery |
| `dskwin.inc` — the mount buffers | 2,336 | `.lowbss`, needed as-is |
| `mouse.inc` — serial mouse | 3,538 | **the cursor half is not wanted** |
| the DOS core from `apps/dos/` | ~12,000 | ESTIMATE; the box's image is 30,731 and most of it is window |
| PSP, environment, MCB chain, `dos_load` | ~1,500 | existing source |
| the shim (§5) | ? | **the unknown** |
| | **~31 KB + shim** | against 39 |

> **MEASURED, and the table above is 5,100 bytes light** —
> docs/reports/KERN-DOS-BUDGET-2026-09-13.md §2. Today's tree, `kern_small`,
> `.text` + `.cold` + `.bss` + `.lowbss` per file: `disk.inc` **7,206**,
> `diskw.inc` **5,473**, `dskwin.inc` **2,336**, `mouse.inc` **3,698** —
> 18,713 against 17,767. And the DOS core row is the one that moved: it is
> **17,667** (14,636 of image and 3,031 of bss, measured by symbol span with
> `tools/os88doscost.py`) and not ~12,000 + ~1,500, because *"most of it is
> window"* is false — the window half is 38% of the image, not most of it.
> The 1,500 for the PSP, environment and MCB chain is already inside that
> figure rather than beside it.
>
> **Floor 36,380 = 35.5 KB against a budget of 39,424 = 38.5 KB**, so **3.0 KB
> is left for the shim and any cache**. The levers below stop being an
> ordering suggestion.

### 6.1 The levers, in the order they should be pulled

1. **Drop the file-window handle layer.** §96.11's 8 KB cluster-aligned window
   exists because os8088's file API is by-name and whole-file. Under
   `kern_dos` the FAT chain is right there, so `AH=3Fh` reads straight into the
   program's buffer. **−8 KB of buffer and ~−3 KB of code**, and it makes the
   box *more* like DOS rather than less. This is the single biggest lever and
   it should be taken first.
   **MEASURED: the 8 KB is a heap CLAIM** (`dos_wseg` holds a segment), so it
   is not in the floor at all and comes off the *budget*; the code half is
   **1,803** of `dos_fh_*`'s 2,186, the other 383 being the handle TABLE,
   which stays because DOS needs handles whatever is under them.
2. **Drop the cursor half of `mouse.inc`.** INT 33h needs the packets and the
   scale; nothing draws an arrow. ESTIMATE −1.5 KB.
   **MEASURED at 1,440, so the estimate was right — and there is a second
   781 beside it nobody counted**: `kbm_*`/`kbd_*`, the keyboard, which
   `kern_dos` does not want either (no event ring to feed, and the ROM's own
   `int 09h`/`int 16h` serve INT 21h's character input). `mouse.inc`'s
   carried share is ~1,007 of 3,419.
3. **Drop `diskw.inc`'s long-operation machinery** — the batch bracket, the
   progress widget, the copy engine (§22.24/§22.25 are the file manager's).
4. **One volume class.** BIOS floppies and the boot partition; no driver
   volumes, no RAM disk, no redirected volumes. This also removes `DVK_FILE`'s
   refusal paths, which several `dos_k_*` doors carry.
5. **No console.** The program owns the screen; `AH=02h`/`AH=09h` go to the
   ROM's teletype, which is what they do inside the fsx bracket today.

### 6.2 …and the cache, which is the interesting one

At ~31 KB plus shim, a **16 KB read-ahead** takes the program to ~587 KB.
Over the 580 the hogs want, but only by seven.

> **MEASURED: it cannot be funded out of slack.** The floor leaves 3.0 KB, and
> 7.1 KB with levers 1 and 2 taken — before the shim is written. So the
> purgeable shape is not the nicer of two options, it is the only one that
> works, and it has to be purgeable in the strong sense: claimed only when the
> program has not taken the memory, rather than merely given back on demand.

**Make it purgeable** — approved.  `kern_dos` claims the cache at the top of free memory
and gives it back the moment the program's own `AH=48h` needs it. A hog that
takes everything at startup gets its 603 KB and no cache; a modest program
that never asks gets 587 KB and a much faster disk. That is os8088's own
`MEM_PG_*` idea (§50.6) in miniature, it is ~50 bytes of the allocator, and it
means the cache never has to be argued about against the target.

---

## 7. The handoff, in order

With arm 3 picked and the program named, on `dos_run`'s wake:

1. **Confirm** (§9, floppy-only machines) or **hibernate** (§8).
2. **Walk two chains into extent lists, while the file layer is still alive**:
   `DOS.O88`'s `kern_dos` part, and `HIBERNAT.IMG`. §87.5 step 2 is the code.
3. **Bank the transport facts** the OS already knows: the `int 13h` unit, the
   partition base, sectors per track, heads (§87.5 step 1) — and the volume
   table, so `kern_dos` skips §18.97's floppy probe entirely.
4. **Bank the launch block**: the program's name, its arguments, the working
   directory's cluster, the environment, the arm, and `[dos_memkb]`.
5. **Tear down**: `cp_flush_close`, `drv_shutdown`, `gfx_lock`, `vid_reboot`
   to text — §87.5 step 3's order, unchanged, because it is the order a
   restart already uses.
6. **Stage into the text framebuffer**: the stub, the `kern_dos` extents, the
   `HIBERNAT.IMG` extents, the transport facts and the launch block.
7. **Jump into the stub.** It reads `kern_dos` low, copies the restore extents
   and the launch block into `kern_dos`'s own image — *not* video RAM, because
   the program will write there — and jumps to `kern_dos`'s entry.
8. `kern_dos` hooks INT 21h/22h/23h/24h/33h, builds the PSP and the
   environment, loads the program with `dos_load`, and runs it.

**Step 7's copy out of video RAM is the one new idea in the sequence**, and it
is forced: §87's stub is the last thing to run before the restored kernel, so
it never had to survive a program.

---

## 8. The return, on a machine with a hard disk

**THE MACHINE GOES ALL THE WAY ROUND, AND THAT IS THE DESIGN RATHER THAN A
SHORTCUT.** The hibernation image is written before the program starts,
`kern_dos` restarts the machine when it exits exactly as it does on the
floppy arm (§9), and the fresh kernel's own `hb_probe` finds the pointer and
resumes it without asking. There is no second stub, no second extent walk and
no second copy of the transport facts inside `kern_dos`.

1. The box asks for arm 3 on a machine with a fixed disk, so the record it
   posts says *hibernate first*.
2. `hbm_dosrun` writes the image and the pointer — `hbm_hib` steps 1 to 6,
   factored — with a byte in `HIBERNAT.PTR` meaning *this was not the user
   leaving the machine*. Then it hands over as W5 already does.
3. The program runs. On `AH=4Ch` `kern_dos` leaves the exit code in the BDA's
   intra-application area and issues `int 19h`, which is `kd_leave` unchanged.
4. The fresh kernel boots, `hb_probe` finds the pointer, and the flag turns
   what would have been `UI_RBQ_ASK` into `UI_RBQ_RESUME`. §87.5's resume runs
   as it always does, and the session comes back with the DOS window in it.

### 8.1 Why not the direct restore, which is what this section used to say

The obvious design — `kern_dos` copies §87.5's stub and the banked
`HIBERNAT.IMG` extents back into the text framebuffer and jumps into it — is
**~1,200 bytes of `kern_dos`'s image**, and every byte of that image is a byte
off the DOS program:

| | |
|---|---|
| `hbs_stub` | **441** bytes, measured |
| `HS_UNIT`..`HS_NX`, `HS_WAKE`, `HS_CLK` | 22 |
| the extent list | **6 bytes an extent**, and `HS_XMAX` is 1,280 |

and the extent list is the part that cannot be bounded cheaply, because the
one place it could live is `kern_dos`'s own image: **the text framebuffer is
not available**, since the DOS program prints into it, and there is no other
RAM a program does not own. A cap would mean a new refusal on a perfectly
ordinary disk.

**`KD_IMG_KB` is 34 and the program has 586 KB against a 600 KB target**
(§1; it was 61 and 559 before §6.1's levers were pulled, and 589 before
§96.40.5 bought 1.44MB floppies back at 3 KB), so 1,200 bytes is
1 KB of the one quantity this plan is a budget for —
spent permanently, on every machine, to save time at the end of a program.

What it saves is **~2.1 seconds, once**: a hard-disk boot is 2,087 ms
(docs/plans/completed/BOOT-PERF-PLAN.md) against a restore that is ~2 s on
iron and a write that is ~2 s (§2.2, measured by the owner on three machines
including a real 5150). So the round trip is ~4 s direct and ~6.1 s round the
houses, at the end of a session the user spent minutes in.

**And the reboot route removes a whole class of the defects §11.2 is about.**
Five of wave 5's seven were *the thing on the other side of the handoff is not
the machine this code was written against*; a second stub with a second extent
walk and a second set of transport facts is five more chances at exactly that.

### 8.2 The exit code rides in the BDA, and that is MEASURED

§12 question 3 asked how the code gets home, since the restore comes back over
everything including `kern_dos` and including the BDA. On this route it does
not have to survive the restore — it has to survive **`int 19h` and a boot**,
which is a different and much weaker requirement.

`0040:00F0` is the intra-application communication area, sixteen bytes the
BIOS sets up at POST and never touches again. `int 19h` is the bootstrap
loader and not POST, and os8088's own boot writes nothing below `0x0600`.
**Verified on the machine**: a magic poked there before `kern_dos`'s `int 19h`
reads back byte for byte after the ROM's bootstrap, after stage 2, and at a
settled desktop.

So `kd_leave` writes `'DX'`, the code and a checksum there, `hbm_ask` reads
them beside the pointer, and `hbm_res` stages the code for the stub to hand to
`hbm_wake` in a register. The window then finds `DST_RAN` with a number.

---

## 9. No hard disk: the first time this OS throws work away

There is nothing to hibernate to, so the machine cannot come back. The program
runs, and then the machine **reboots**.

This is the first deliberately destructive action in os8088 and it should read
like one:

- The third radio arm is **greyed with the reason** on a machine with no fixed
  disk (§47 — grey a fact, never a guess): *"needs a hard disk to come back
  to"*. It is not offered and then refused.
- Unless the user asks for it, which they will. So: a **second, separate
  confirmation** naming what is lost — *every open window, every unsaved
  document, and the machine restarts when the program exits* — with the
  destructive verb on the button and **Cancel as the default**.
- The confirmation is not a toast and not a rider on the Memory page. It is
  its own window, at the moment of launch, and it lists the open packages by
  name so the loss is concrete rather than abstract.

**The greying predicate is `hb_pick`'s** (§87.2) — the same question hibernate
already asks about whether there is a fixed disk to write to — so there is one
answer and not two.

---

## 10. What arm 3 LOSES, stated rather than discovered

Arm 3 is **not a superset of arm 2**, and the Memory page has to say so:

- **no packet driver** — §96.23's Crynwr interface is `ETHER.DRV` over the
  kernel, and neither is there. **A future phase may reopen this**: a thinner
  DOS-side rework of `ETHER.DRV`, offered as an option rather than carried
  always, and §12 question 7 is the one thing the design must not box out;
- **no windowed mode** — the program is fullscreen by definition;
- **no console capture** (§96.34), no Task Manager row, no clipboard;
- **no `DOS.O88` overlay** and no second instance;
- **a launch and an exit cost ~160 `int 13h` calls** (§2.2);
- **and on a floppy-only machine, everything open** (§9).

---

## 11. Waves

| | what | gate |
|---|---|---|
| **W0** | **BUILT** (SPEC.md 96.36). `[dos_keepc]` is three-way over `os88ui_rad` — the control's **first caller in the tree** — and arm 3 is greyed with its reason on the glass. +364 package bytes, +6 bss, **zero kernel**. | `soak -k dosmem`, and its three verified failures |
| **W1** | **DONE** — docs/reports/KERN-DOS-BUDGET-2026-09-13.md. Arms 1 and 2 confirmed at 449/481 KB with a 32 KB cache between them, the floor re-derived at **35.5 KB against 38.5**, and the hibernate round trip at **~4 s on iron** (§2.2 — the 43.4 s this first reported is MartyPC's XT-IDE PIO and not the field's controller). | the report, and `tools/os88doscost.py` to re-derive it |
| **W2** | **DONE, and the seam held with three breaches to fix** (§3, SPEC.md 96.4.2). Two new doors, +24 package bytes and no kernel byte; `tests/unit/t_dosseam.py` is the gate and walks the CALL GRAPH. | `soak -k dosseam` — **soak and not the fast this row first said**, by docs/WRITING-TESTS.md §2.1 rule 1 |
| **W3** | **DONE, and the reuse pays by a factor of 130** (SPEC.md 96.37). The shim is **92 bytes** against 11,935 of kernel disk code; `kerndos/kerndos.asm` mounts a FAT12 floppy and reads a file with no scheduler, no window manager and no API table under it. | `soak -k kerndos`, checked against `os88fat.py` |
| **W4** | **DONE, and the core needed no splitting** (SPEC.md 96.38). `kerndos/kdos.asm` is W3's root plus `apps/dos/dos.asm` **whole and unedited** plus `kerndos/kdback.inc`'s twenty-two doors; `KDHELLO.COM` reads **500 KB above its own PSP** against the windowed box's 449, and exits AH=4Ch back into `kern_dos`. | `soak -k kdos`, `-k kdfar` |
| **W5** | **DONE — a DOS program runs with the whole machine and gives it back** (SPEC.md 96.40, 96.40.1, 96.40.2). §7 steps 2–8, ending in `int 19h`; no hibernate yet. The measurement is one comparison: **560 KB above the PSP against 438 in the window** (589 against 437 since W8), same program, same disk, same DOS core. W5a is the launch block's ABI and `kern_dos`'s real entry, W5b the disk measurement (docs/reports/KERN-DOS-PART-COST-2026-09-14.md), W5c the handoff itself — four resident kernel bytes, everything else in `HIBER.DRV` — and W5d the gate. **Seven defects were found by building the gate and every one is in its header**; §11.2 is what they came to, because five of the seven are one shape. | `soak -k kdhand -k kdapi -k kdos -k kdfar -k kdpart` |
| **W6** | **DONE — the machine goes away, runs DOS, and comes back** (SPEC.md 96.41). §8's reboot route, not §8.1's direct restore, which is ~1,200 bytes of the program's own arena. **EIGHT RESIDENT BYTES, measured**: `.bss` +2 for `hb_doscode` (which lives between `hbm_ask` reading the BDA at the boot and `hbm_res` staging it for the wake, two separate loads of the image with a posted restart between them, so it cannot be module data) and `.cold` +6 for the one line in `hb_probe` that defaults it. Everything else is `HIBER.DRV`'s, the box's, or a field in a record that already existed. The exit code rides in `0040:00F0` and `HS_DOSCODE` needs no stub code at all, `hbm_wake` already reading that segment. §8.2 answers open question 3 and the answer got EASIER when the route changed. | `soak -k kdreturn -k kdhand -k hibernate -k hibernatedrv` |
| **W7** | **DONE, and it is OFFERED rather than greyed** (SPEC.md 96.42). §9's second bullet won over its first: the memory is a real want and the machine really can deliver it, so a greyed control would be refusing a capability rather than reporting a fact. What the question names is the fact — there is nothing to come back to — at the moment the user commits. `OS88UI_ADANGER` is a fourth alert set (SPEC.md 75.3.3), **Cancel at index 0** so the ring and Enter are the safe answer; sixteen bytes of any package that opts into the alert and **no kernel bytes at all**. The shipped `DOS.O88` is BYTE-IDENTICAL — the whole of it is `%ifdef DOSKPART`. | `soak -k kdhand`, and the alert rows beside it |
| **W8** | **559 → 589 KB, and §6.1's list was aimed at the wrong question** (SPEC.md 96.43, 96.43.1, 96.43.2). It asks which FEATURES `kern_dos` does not want; the one that pays is *which code could not do anything even if it were called* — `KERNEL_SEG` is `KD_SEG` there, so every surviving `OSAPI_*` lands in 96.40.2's refusal table. The **packet driver and the cable translation** (−4,684), the **console** (−12,247, 6,863 of it `.bss`), the **FORMAT module** (−1,297), the **window half of `dos.asm` with `os88ui`/`os88line` and twenty-nine `DBSS` rows** (−8,374) and `disk.inc`'s four **`.ovlw` spans** (−762): `KD_IMG_KB` **61 → 34**. Then **`KD_LOW_KB` 8 → 5 on a measurement** — the stack's water mark is **134 bytes** of 4,864 (`KDSTKDIAG=1`, `tools/kdstkwater.py`). **The shipped `dos.o88` is md5-IDENTICAL and both kernels are byte-identical**, which is what a gate-only change owes. **600 KB is NOT reached**; §11.5 is the arithmetic and what is left is one trade and a long tail. | `soak -k kdhand -k kdreturn -k 'dos*'` |
| **W9** | **W9a IS BUILT** (SPEC.md 96.44, 96.44.1): the core is one file behind two defines and NOTHING WAS MOVED. §96.43's gating turned out to do both jobs — `%ifndef KD_BACKEND` is the window half, `%ifndef DOS_EXTCORE` is the core, and what is marked neither is the container every build wants. **141 spans marked**, and the marking is CHECKED twice: `dos.o88` came out md5-identical through it, and `build/doscore.bin` is in `all` so a span marked wrong fails where neither host would notice. **The core is 14,409 bytes and names THREE things outside itself** — `os88_image_end`, `DVOL_MAX` and `dos_bevec` — which is §4.1.3's *core → box is ZERO* made good. The doors store a `DBE_*` ORDINAL now and `dos_be_go` resolves it (96.44.1), nine bytes once; `dos_be` was DEAD and was the table that change needed. **W9b** is `org CORE_ORG`, the entry table and the `CORE_ORG` budget; **W9c** the loader and the three parts, and **BOTH ARE BUILT** (SPEC.md 96.44.4, 96.44.5). The core goes LOW rather than above both hosts - 4.1.3.1's measurement was overtaken by W8, and the correction is 4.1.3.1.1. `DOS.O88` is **42,962** bytes against 53,789, the program is handed **585 KB**, and `kdhand`, `kdreturn`, `kdmix` and `kdpart` are the four gates. The disk case is measured twice and the second reading is what SHIPPED: a parted package's image is RAW (`os88pkg.py` refuses `--compress` with parts), so §96.44.4's two-piece shape cost the 360KB system disk **30 of its 50 free clusters** and **+932 ms a launch**, and the four-piece one costs **17 and +750 ms**. `$(SYSROOT)` points at it now, so arm 3 is LIVE on every shipped disk rather than on a gate disk — and the two gate disks are deleted, having built byte-identical to `$(IMG360)` and `$(IMG)` the moment it flipped (SPEC.md 96.40.3). | the box and `kern_dos` both run against one core part; `soak -k 'dos*'` |

**W0 and W1 land before anything is designed further.** W2 is the go/no-go for
the whole shape; W3 is the go/no-go for §4's reuse.

### 11.2 What W5 cost, and the one shape five of its seven defects shared

The handoff worked on the first build of every piece and ran nothing: the
post, the teardown, the stage, the stub and the entry were each correct in
isolation and the machine did six different wrong things before a program
printed a line. **Five of the seven are the same sentence** —

> **the thing on the other side of the handoff is not the machine this code
> was written against, and the difference is silent.**

- `hbm_dosrun` read `[hb_dosoff]` **after** `mov ds, [hb_dosseg]`. Both words
  are `KERNEL_SEG`'s and the line above is what stops DS being it, so the
  second read came out of the *poster's* image at that offset and copied 543
  bytes of somebody else's bss over the record. It assembles, it runs, and the
  magic check is the only reason anyone found out.
- `dsk_find_name` was handed a name in the **module's** image. It compares
  `DS:SI` against `DS:DI`, so the name was read at that offset in the kernel's
  segment and matched nothing: *"DOS.O88 is not on that disk any more"* about
  a file in the folder the module had just stood in. `api_name` is the answer
  and it costs no resident bytes.
- The stub's expander read the part as a **classic LZ4 block**. §20.13.7's
  stream is a T word, the symbols and a raw tail; a classic decoder reads the
  T word AS A TOKEN, and `05 00` is *"copy nine bytes from 0xFC00 back"*, so
  the image landed nine bytes along with the header still holding whatever was
  there before. It is the third reader of that format in the tree.
- `int 1Eh` was left naming `KERNEL_SEG:dsk_dpt`. `kern_dos` lands on that
  same segment with **its own** table at a different offset, so the ROM read
  code as an EOT and a gap length — and the symptom was *"the disk could not
  be mounted"* about a floppy whose BPB `int 13h` had just read perfectly.
- `.bss` and `.lowbss` are `nobits` and **nothing that puts the image in
  memory writes them**. Wave 4 never saw it: a machine four seconds out of
  POST has zeros above the image. The handoff arrives with the outgoing
  kernel's data at its own offsets, which is a volume table, a FAT window and
  a handle table that all look plausible and belong to another operating
  system.

The sixth is the same shape one layer out and is the one with a general
answer. **`KERNEL_SEG` IS `KD_SEG`**, so every `call OSAPI_X` that survives
into the image is a far call into `kern_dos`'s own code. Wave 2 measured the
LOAD path at 21 procs reaching none of them and that measurement stands; what
it did not cover is the RUN path, where `dos_getkey` samples `dos_mou_read`
between `int 16h` checks — which is **every DOS program that waits for a
keystroke**. So the image now carries a wall of refusals at the published cell
offsets (SPEC.md 96.40.2): 1,432 bytes of a rung with thousands spare, not one
byte of the arena, and a stray call becomes a wrong answer rather than a wild
jump. `tests/unit/t_kdapi.py` keeps its two ends on `apps/os88api.inc`.

The seventh is not that shape and is worth its own line, because it is a
property of **the ROM**: `int 19h` takes no documented input and GLaBIOS reads
the boot drive out of `DL` as it finds it. `dsk_fdd_park_x` leaves `DL` = 0 by
falling out of its own loop, which is why the desktop's Restart has never
shown it; a routine that simply calls `int 19h` hands the ROM whatever the DOS
program left, and the bootstrap **returns** rather than boots.

**The lesson for W6 is the whole of the above read forwards.** The return is
the same handoff in the other direction and every one of these questions has a
mirror: what segment is that pointer in, what did the outgoing side leave in
that memory, and what does the ROM think it is holding.

### 11.4 W7's finding is about a WAKE, and it is the box's own idiom biting

**`[dos_wok]` has three states and not two.** A launch is a posted wake and a
wake is a KICK (SPEC.md 74.1): tearing the alert down repaints, and the box is
still `DST_READY` when the next wake lands — so a flag that only recorded
*confirmed* put the question straight back up, and **Cancel could not be
answered at all**. It is the same shape as `dos_wake`'s own comment about
`DST_RAN` being set BEFORE the run ("a second wake arriving for any reason
finds DST_RAN and does nothing, rather than launching the program twice"), one
question earlier: a refusal has to be recorded for exactly the same reason a
launch does.

**And the hook belongs on `dos_wake`, not on the Run button**, which is where
it was nearly put. A `.LNK` carries the arm (SPEC.md 96.21) and an association
launch never passes through `dos_go` at all, so a shortcut written on a machine
WITH a hard disk would have ended the session on a machine without one with
nothing asked.

### 11.3 ...and W6 found four, three of which are that same lesson

**The flag was set in the STAGE and the stage is filled forty lines later.**
`hbm_dosrun` ORed `KDLF_HIBER` into `KDS_LB` at step 3b and step 5's
`rep movsw` then wrote the box's own block over it. It read as the whole
return working and `kd_leave` posting nothing — a machine that comes back with
no exit code in it. The record's copy is the one to patch.

**`[sch_lock]` is not a flag about the gfx lock.** Step 4 must not take the
lock when `hbm_wrimg` already has it, and the obvious test — is `[sch_lock]`
non-zero — means nothing: `dsk_xfer` raises it around every `int 13h` and
`ui_task` holds it for its own reasons. It skipped `cw_gfx_lock` on the
**floppy** arm, where nothing had been taken at all, and the teardown then ran
unlocked. A byte the routine sets itself is the only thing that knows.

**SI is the launch block until something prints.** `kd_leave` reads `[kd_lbp]`
at the top, prints two strings — each of which loads SI — and then read the
boot drive out of `[si+KDL_UNIT]`, which by then is a character of *"Press any
key to restart."*. `int 13h` answers AH=01 to that and the bootstrap returns.

The fourth is not ours and is worth its own line, because it had been WRONG in
a tool since the day it was written and nothing could see it:
`tools/os88hdd.py` spelled a 'CZ' file's unpacked size `n`, which is the
variable holding the CLUSTER COUNT three lines down — so `nextc` advanced by a
BYTE count after every compressed file. The volume stayed **self-consistent**
(FAT chain, directory entry and data all used the same wrong number), so a
four-file fixture was merely very sparse and booted perfectly. It surfaced as
*"DOS.O88 does not fit the volume"* on a 32MB disk holding 173 KB.

**And a test-harness one worth keeping.** `ui.up()` is not a wait for a
RESTARTED machine: a warm boot clears no bss — `hb_probe` says so about its own
three words — so `desk_rows` and `menu_nbar` still hold the last session's
values while the next one is in the ROM, and `up()` returns on the BIOS banner
with every reading after it taken from a machine that has not booted. Zeroing
them first is worse: at that moment those addresses are `kern_dos`'s running
code. The VIDEO MODE is the honest question — os8088's desktop is graphics on
every adapter and everything between is not.

### 11.5 W8 got 21 KB and the last 20 are W9's — the arithmetic

The wave's own finding is that **§6.1's list was aimed at the wrong question.**
It asks which FEATURES `kern_dos` does not want; the question that pays is
*which code, under this root, could not do anything even if it were called* —
and §96.40.2's refusal table makes that a large, exactly-knowable set. Two of
the three things W8 took are not on §6.1's list at all (the packet driver and
the cable translation; the FORMAT module), and the one that is — lever 5's
console — was the largest single item on it and was priced at nothing.

**What it came to:**

| | bytes | |
|---|---:|---|
| the packet driver + the cable translation | −4,684 | dead: `OSAPI_DRV_CALL`, `OSAPI_MEM_CLAIM` |
| the console (`dosc.inc`, `os88con.inc`, `os88cp437.inc`) | −12,247 | lever 5; 6,863 of it `.bss` |
| the FORMAT module (`diskw.inc`'s `.modf`) | −1,297 | dead: no `mod_need` in this root |
| the window half + `os88ui`/`os88line` + 29 `DBSS` rows | −8,374 | 96.43.2: twenty bands, not forty sites |
| `disk.inc`'s four `.ovlw` spans | −762 | no `kmain` to run a boot overlay |
| | **−27,364** | `KD_IMG_KB` **61 → 34** |
| `KD_LOW_KB` 8 → 5 (§96.43.1) | **−3,072** | measured: 134 bytes of stack used |
| | **−30,436** | **559 KB → 589 KB** |

**Three of §6.1's five levers did not apply**, and that is worth recording so
the list is not re-derived:

- lever 2 (`mouse.inc`'s cursor half) is **already taken**: `kern_dos` does not
  include `mouse.inc` at all.
- lever 1 (drop the file-window handle layer) is **a rewrite, not a cut**, and
  the plan's *"the FAT chain is right there"* is the part that is wrong. What
  is right there is `dskw_read_at_x`, and its preconditions are the KERNEL's —
  a cluster-aligned offset and a cluster-multiple capacity, `diskw.inc:2683` —
  so §96.11's window exists for exactly the same reason under `kern_dos` as in
  the package. Its 8 KB is real and comes off the arena top, but taking it
  means writing an unaligned read path, which is code ADDED. The cheap version
  is to shrink `DOS_WKB`, and that is a speed trade rather than a saving.
- levers 3 and 4 (trim `diskw.inc`, one volume class) are in **kernel files**,
  where the gate must be `%ifndef KD_BUILD` and the kernel must measure
  byte-identical after it. That is how the FORMAT module went, and it is the
  shape the rest would take — the remaining `dsk_*`/`dskw_*` is 11,784 bytes
  and no single row of it is large.

**AND THEN THE WINDOW HALF WENT TOO, on a correction to this section.** The
paragraph that stood here said it could not be gated without threading a
`%ifndef` through forty call sites. **Forty was a count of CALL SITES where
the quantity that matters is CONTIGUOUS RUNS**: classify every top-level label
and the window's are **twenty bands** (SPEC.md 96.43.2), each one gate pair.
`os88ui.inc`, `os88line.inc`, twenty-nine `DBSS` rows and `disk.inc`'s four
`.ovlw` spans go with them — **−8,374 and −762** — and the check is that the
shipped `dos.o88` is **md5-identical** either way, which is a stronger
statement than any test. `KD_IMG_KB` **43 → 34**, the program **580 → 589 KB**.

#### 11.5.1 The last 14 KB — the floor to the byte, the top measured

**The FLOOR is four terms and every one of them is exact**, which is the half
of this that a wave can act on:

| | bytes |
|---|---:|
| below `KD_SEG` — the IVT and the BDA | 1,536 |
| `KD_IMG_KB` 34 | 34,816 |
| `FAT_SEG` — `DSK_FAT_SECS` × 512 | 4,608 |
| `KD_LOW_KB` 5 — the mount buffers and the stack | 5,120 |
| **the floor** | **46,080** |
| a 640 KB machine, less the floor | 609,280 |
| less §96.11's file window | −8,192 |
| less the environment MCB and the PSP (`DOS_PSPP`, 10 paragraphs) | −160 |
| a 640 KB machine, what is left | 600,928 |
| **what `tests/kdhand.py` reads** | **586 KB** |

**THE LAST ROW IS MEASURED AND THE ONES ABOVE IT ARE DERIVED, and they do not
meet to the byte on purpose**: `tests/doscom/hello.asm` prints
`([es:0x0002] − CS) >> 6`, so its answer is PARAGRAPHS TRUNCATED TO KB and
the last 1,023 bytes of the arena are invisible to it. Quote 586 as the
figure and this table as where the other 54 KB went; do not subtract two of
these rows and call the difference a measurement.

**Two of those rows are corrections and both were quoted for a cycle.**
`FAT_SEG` was 1,024 here and is 4,608 — §96.40.5 is why, and it is the
single largest term this plan has ever added back. `DOS_PSPP` was quoted as
96 paragraphs: it is **10** (`DOS_ENVP` 32 is the environment, and the
environment sits BELOW the arena's own accounting, not between the floor and
the PSP), so the old table overstated the overhead by 1,376 bytes while
understating the floor by 3,584 — two errors of opposite sign, the same
shape docs/KERNEL-MEMORY.md's own `+46` had.

So **600 KB (614,400) needs the WHOLE file window AND about 5.5 KB more of
floor** — where before §96.40.5 it needed 3 KB, and the media fix is what
moved it. **Neither is lying around.** The window cannot be deleted, only
shrunk, and shrinking it is geometry-dependent: it is
`max(cluster, largest multiple of cluster ≤ DOS_WKB × 1024)`, so on a 360KB
floppy's 1 KB cluster `DOS_WKB = 1` buys **7 KB** and on a hard disk with 8 KB
clusters it buys **nothing**. And 2,422 bytes of image means removing a
FEATURE: the biggest families left are `dsh_*` at 4,989 (the built-in
commands, which `AH=4Bh` and Microsoft C's `system()` reach — §96.30), the
disk layer's 11,214 with a largest single family of 743, and a core whose
biggest single item is `dos_ivt`, 1,024 bytes of `.bss` that `kd_leave` needs
intact to reach `int 10h`, `int 13h` and `int 19h` after the program has
scribbled on the vectors.

**So the last 11 KB is a DECISION and not a size pass**, and §6.2's *make it
purgeable* — approved there for the read-ahead cache — is the shape that
answers it without a trade: claim the window at the top and give it back the
moment the program's own `AH=48h`/`AH=4Ah` needs it, so a hog gets 597 KB and
no window and a modest program keeps 589 and fast file I/O. That is MCB-chain
work and it is the one option that costs nobody anything.

**600 KB is reachable and W8 stops at 589**; §13's first bullet asks for that
to be said rather than shipped quietly, and this is it.

**W9 is unaffected by any of this.** Its case was never the memory: it is
**+19 clusters of a 360KB system disk against 26**, the DOS core being in
`DOS.O88` twice. What W8 changes is that the core is now the *only* thing in
the part worth extracting, which makes §4.1.3.1's near join easier to size
rather than harder.

### 11.1 What W0 came to, and the one line W6 changes

The wave cost 364 package bytes and no kernel byte at all, and it landed
where it was aimed — but three things in it are worth writing down, because
two of them are the sort of thing that gets re-derived.

**The greying is ONE routine and W6 replaces its body.** `dos_mem_whole`
answers *may the program have the whole machine?* in CF with the reason in
SI, and today it refuses unconditionally with *"not in this build yet"*.
Three consumers read it — the DIS bit, the caption, and `dos_mem_fix` at the
block's commit point — so when the mechanism exists, that body becomes
`hb_pick`'s question (§9) and **nothing else in the package moves**: not the
layout, not the record, not the three call sites, not the `.LNK` format.

**Consumer three does not belong on `dos_run`,** which is where it was put
first. A `.LNK` written on a machine that HAS the feature can carry
`DOS_MEM_WHOLE` to one that does not, and a greyed control refuses a click
and not a file — but `dos_run` is not reached until a launch, and an empty
path box never reaches it at all. It sits on `dos_mem_take` instead, which is
the memory block's one commit point (all four of its callers are a page being
left or a launch), so the page comes back showing what the machine will
really do.

**The third FIGURE is deliberately absent.** The two on the page are
`OSAPI_MEM_AVAIL_LVL`'s and `OSAPI_MEM_AVAIL`'s real answers; what arm 3
would give the program is not a number this build can ask anything for, and
§47 rule 5 refuses a guess sitting beside two measurements. It arrives with
W1, which is the wave that measures it.

---

## 12. Open questions

1. **How big is the shim, and can levers 3–5 find the rest?** (§5) The plan's
   largest unknown, and W3 answers it. A shim near the size of a
   purpose-written FAT reader reopens §4. **W1 sharpened this**: the budget
   after the two priced levers is 7.1 KB, and levers 3, 4 and 5 are each a
   subset of a file rather than a family of symbols — so pricing them means
   classifying `disk.inc` and `diskw.inc` proc by proc, which is W3's work and
   not a measurement that can be taken without it.
2. **Does the DOS core assemble outside a package at all?** It is `org 0` with
   bss at `os88_image_end` and a three-byte dispatcher header (§20). W2 should
   check this, not assume it. **PARTLY ANSWERED**: W2's walk shows the
   reachable core calls into none of the package libraries — `os88ui.inc`,
   `os88line.inc`, `os88parts.inc` and the socket layer are zero sites — and
   reaches the console only on the arm `kern_dos` never takes. What is left is
   the mechanical half, `org 0`, `os88_image_end` and the header, and that
   cannot be answered without doing the split: it is W3/W4's, not a scan's.
   **ANSWERED BY W4, and better than expected**: it assembles WHOLE, and the
   mechanical half is three things — the three container macros assert their
   own file offsets so they are gated out; `os88_image_end` becomes a label in
   the root's own `.bss` (the DBSS table is offsets from it either way, and the
   name is all that has to be kept); and `org 0` was never the obstacle,
   because `-f bin` sections are laid contiguously from wherever the root puts
   them. §4.1.2 carries what that means for the split that is no longer
   needed.
3. **How does the exit code come back?** **ANSWERED BY §8.2, and the question
   got easier when the route changed.** The direct restore comes back over
   everything including the BDA, so nothing `kern_dos` writes survives it —
   which is what made this hard. On the reboot route the code only has to
   survive `int 19h` and a boot, and `0040:00F0` does: it is the BIOS's
   intra-application area, set up at POST and never touched again, `int 19h`
   is the bootstrap and not POST, and os8088's own boot writes nothing below
   `0x0600`. **Verified on the machine**, byte for byte, at a settled desktop.
4. **XMS.** The box publishes `OSAPI_XMEM_*` to DOS programs today. Does
   `kern_dos` carry an XMS provider, or does arm 3 lose extended memory too?
   §87.7 already owes extended memory to hibernate, so the two are related.
5. **Which volumes does the program see?** A: and B: from the BIOS is the
   floor. Does the boot partition appear as C:, and does `kern_dos` carry
   FAT16?
6. **Does `kern_dos` need `diskw.inc` at all in W4?** A read-only first arm is
   a smaller target and many programs never write. It is not the shipping
   answer but it may be the right W4. **ANSWERED: NO, AND THE QUESTION WAS
   BACKWARDS.** `diskw.inc` is not the write path, it is the **by-name file
   I/O layer** — docs/plans/completed/KERN-SMALL-MODULE-SPLIT.md found the same
   thing one wave earlier for a different reason — so `dos_k_read`, the door a
   read-only arm is built out of, IS `dskw_read_x`. Leaving it out does not buy
   a smaller W4, it removes the ability to load the program. W4 carries it
   whole and the write verbs came along for free; what a read-only arm would
   really cut is `dskw_write_x` and its neighbours, which is a §6.1 lever and
   not a wave.

7. **Where would an optional packet driver go?** A thinner DOS-side rework of
   `ETHER.DRV` is a named future phase, so the design must leave room: the
   launch block should be able to say *"and load this too"*, the low-memory
   layout must not assume `kern_dos` is the only resident piece, and the
   arithmetic on the Memory page has to be able to report what the option
   costs — because it comes out of the same 39 KB and the user is the one
   trading it against their program. **Nothing here needs building now; what
   is needed now is not making it impossible.**

---

## 13. What would kill this

Written down so it is recognised early rather than argued about late:

- **The budget.** If `kern_dos` measures over ~39 KB after §6.1's levers, the
  600 KB target is not reachable by this route and the honest answer is to say
  so rather than ship 560.
- **The seam.** If the INT 21h core turns out to reach the kernel outside the
  `dos_k_*` doors in ways that matter, the port stops being a back end and
  becomes a rewrite.
- **The stub's reach.** §87's stub reads through `int 13h` rung 0. A machine
  whose hard disk is IDE rung 1 (§52.1) cannot hibernate today (§87.7 owes it)
  and so cannot use arm 3 either. That is an existing limitation inherited,
  not a new one — but it decides who the feature is for.

## 14. The setup area and the arena page — MEASURED, then decided

Asked by the owner as five ideas, with *"give me your feedback before we
actually do any of them"*. Everything below was measured on the tree at
`b73c075` before anything was built, and three of the five answers changed on
the numbers. **The unit is BYTES and not a ratio** — the owner's correction,
and CLAUDE.md's own banner: *"2.5KB is 2.5KB… These are not ratios against how
fat we got."*

### 14.1 What the box actually costs

`DOS.O88` is a loader plus three parts:

| | bytes | |
|---|---|---|
| loader | 2,092 image + 94 bss | |
| **part 0, the UI** | **46,110 unpacked** | **SEGMENT — resident for the session, and straight off the arena on a windowed launch** |
| part 1, the DOS core | 14,632 | ASSET, lazy + compressed |
| part 2, `kern_dos` | 31,975 | ASSET, lazy + compressed |

Only part 0 is the arena's problem. Parts 1 and 2 cost nothing until used.

### 14.2 The five, with the measurement each

**1. The Environment page as its own part — NO, and the number is why.**
Its own routines are 274 bytes (`dos_paint_env` 56, `dos_click_env` 68,
`dos_lnk_env` 85, `dos_page_turn` 39, `dos_page_ttl` 14, `dos_senv_place` 12),
plus 196 of buffer and 80 of line records — **~550 bytes**, because §96.32.2
had already made the page cheap (its env box IS Setup's, one buffer and two
rects). Re-priced at the owner's wider scope — *the whole setup area, counting
every widget as if it were setup-only* — it is **~3.5 KB**: box-side setup
routines 1,589, `os88line_*` 974, `os88ui_rad*` 376, the glyph family 321.
And `os88line_*` is NOT setup-only (`dosc.inc` calls `os88line_resync`/`_draw`
for the console's input line), so the honest figure is **~2.5 KB**, one 4 KB
claim, for the same far-call glue the console would need. **Kept as an option,
not first**: unlike the driver boxes it costs the user nothing, and a config
page may fairly wait a second or two for a disk read.

**2. The console as a droppable part — REAL MONEY, PARKED.**
`os88con.inc` 3,440 (one contiguous run, exact), `dosc.inc` 1,535, `dosh.inc`
811, and `CON_BSS` **6,863** (`con_scr` 4,000 + `con_glyf` 2,048 + `con_band`
640 + 175 of state) = **12,649 bytes, 12.4 KB**. Four things bite: a region
never shrinks, so it must become a second SEGMENT part with far calls across
~40 symbols; the console is a **LOG** and `dos_con_ended` writes the exit line
into `con_scr` AFTER the run, so dropping `con_scr` loses the scrollback and
keeping it saves only 8.6 of the 12.4; the reload is ~4 KB packed ≈ 2-3
`int 13h` ≈ **~1 second on the XT**, on the way back from every program; and
it buys nothing on arm 3. **The owner's framing corrects the last of those and
is the one to keep**: *"all of the arms need memory, dos is a hog. Saving ram
in the OS mode is another program that can run without NEEDING to exit the OS
— that isn't to minimize arm 3's ram needs, saving ram there is 'another dos
program that can run AT ALL' — its just a different target."* Two targets, not
one important and one not.

**3. Fold the env rows onto Setup — DONE** (SPEC.md 96.32.2.1, `277a352`).
−388 resident bytes, more than the 274 predicted, the arrows' rects and the
page-turn machinery being the rest. The fit was MEASURED on all three
adapters rather than argued: CGA 638x197 with **96 px free** below the
Environment field, Hercules and VGA 718x257/638x257 with 156 — against the 48
that three rows at `DOS_EROWH` need. `tests/dosenvfold.py` is the gate.

**4. The RAM page rework — AGREED, and the numbers make it the first job.**
See 14.3.

**5. The disk-cache dropdown — AGREED, with the rungs corrected.** See 14.4.

### 14.3 The driver checkboxes are the biggest lever in the box

**`drv_suspend_x` skips THREE classes, not two** — and the third is the
finding:

```
    cmp al, DRVC_DISK   je .next
    cmp al, DRVC_FILE   je .next
    cmp al, DRVC_NET    je .next   ; §96.23.6 — the packet driver is the only
                                   ; route a DOS program has to the card
```

So **the only thing `OSAPI_DRV_SUSPEND` can take today is the sound driver.**
The kernel already refuses to unload `ETHER.DRV` for exactly the reason the
owner gave: *"we support networking under dos so we don't always unload it —
we're just wanting to give them the OPTION of unloading it."*

And the kernel already knows every figure. `drv_memk` is one word per
`drv_tab` row, and `tests/unit/t_drvmem.py` checks each against the built
`.drv`:

| class | driver | `drv_memk` | reachable today |
|---|---|---|---|
| `DRVC_SOUND` | `SOUND.DRV` | **34 KB** (6 image + 8 DMA + 20 pool) | yes |
| `DRVC_DISK` | `HDD.DRV` | **32 KB** (8 image + 4×6 listing claims) | no |
| `DRVC_NET` | `ETHER.DRV` | **32 KB** (18 image + 14 rings) | no |
| `DRVC_FILE` | `RAMDISK.DRV` | **21 KB+** (`DRVM_PLUS`) | no |
| `DRVC_NET` | `NET.DRV` | 7 KB | no |

Three checkboxes are worth **~85 KB** against the 34 the box reaches now.

**The kernel bill is ~55 resident bytes**, in two pieces:

- **a per-class mask on `drv_suspend_x`** — `BX` = extra `DRVC_*` classes the
  caller may take, 0 = today's set. Bank `BL` at entry (the loop reuses BX as
  the row pointer), and test the bit before each skip compare. The resume side
  needs nothing: `drv_susp` already records what went. **~25 bytes + 1 of
  bss**, and every existing caller must now zero BX — that is the ABI change.
- **a class-keyed info slot** — `AL` = a `DRVC_*`, out `AX` = the KB its
  LOADED drivers hold (`DRVM_PLUS` carried in bit 15) and `CX` = how many.
  **Class-keyed and not row-keyed on purpose**: it is the question the
  checkbox asks, it hides `drv_tab` from the box, and `DRVC_NET` has TWO
  drivers so a row-keyed slot would make the Network box lie. **~40 bytes of
  `.cold` + an 8-byte cell.**

**Two constraints to design around, both real**: unloading `DRVC_DISK` while
the program is ON a hard disk loses the program, and the same for `DRVC_FILE`
and the RAM disk. So the box takes those classes only after the load, or greys
the box when `[dos_vol]` names that transport.

### 14.4 The page itself

The owner's layout, with what each part costs:

- **`Arena: ~xxxKB` at the top, live.** `dos_mem_figs` already asks
  `OSAPI_MEM_AVAIL_MAX` at both ranks; the figure is that plus Σ(unticked,
  loaded) through the new slot. **Keep it on the what-if and never on a posted
  compaction** — SPEC.md 66.4.3, a failed claim is destructive, so opening the
  page must not shed the caches.
- **Two radio arms with subsections**, replacing three arms. Arms 1 and 2
  today are both *inside the OS* and differ only by the cache, which is
  exactly why the cache leaves the radio.
- **"Inside the OS"** — Hard Drives / Network / RAM disk check boxes, each
  labelled with its own KB and greyed with a reason when the driver is not
  loaded or holds the program; plus `Limit:` as now.
- **"Shut down the OS"** — **first option: `Disable the mouse`.** Asked by the
  owner after the rest: *"the other session just found it may be causing
  performance drops - heavy thing to do on a 4.77mhz apparently, and if the
  dos program doesn't care about a mouse then the user can pick not to have
  it on."* **The kern_dos half is already built and costs nothing**:
  `kd_mou_start` reads `KDL_MOUBASE` and a zero there means *"this machine has
  no serial mouse… nothing is hooked, `[kdm_base]` stays zero, and
  `kd_mou_read` answers the still pointer §96.10 already defines"*. What is
  needed is a way to SAY it: the kernel patches `KDL_MOUBASE` into the staged
  block at §87.5 step 5, so the box's zero would be overwritten. A
  `KDLF_NOMOUSE` bit in `KDL_FLAGS`, tested by `hbm_dosrun` before that patch,
  is ~8 bytes of the hibernate MODULE and nothing resident.
- **The hibernate check box is REFUSED**, and the owner's question is the
  reason: *"why would they want to UNCHECK that if they have a hard drive?
  What would they gain from not hibernating?"* Only the image write and ~600
  KB of disk, and a full disk already degrades gracefully (`KDLF_HIBER` stays
  clear and the machine restarts). So the subsection carries the mouse box and
  the arm's own estimate.
- **The disk cache floats out as its own dropdown**, applying to both arms.
  **The rungs are not what they first looked like**: `DSK_RAH_MIN` is 4 slots
  and the assembly refuses 0 outright (*"at 0 `dsk_rah_want` would claim
  nothing and then scan it"*), so the OS-side arms are **Auto / 32 KB (7
  slots) / 18 KB (4 slots) / Off**, where Off is *shed it entirely* — the path
  `DOS_MEM_DUMP` already takes — and NOT a 0-slot rung. **There is no 9 KB**:
  that is `KD_RAH_KEEP`'s ladder INSIDE kern_dos (2 runs), a different
  quantity, and conflating the two is the trap. `SLOW!` goes beside Off, at
  the owner's request.

### 14.5 The order, and where it stands

1. **the kernel's two slots** — ~55 resident bytes for ~85 KB of arena. *In
   progress.*
2. **the page rework**, including the mouse box. *Next.*
3. the env fold — **done**, `277a352`.
4. the console part — parked, 12.4 KB, most work and it taxes the return path.
5. the setup area as a part — ~2.5 KB, kept as an option and not first.
