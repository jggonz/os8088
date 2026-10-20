# The boot ladder: what it reserves, and what it could

**ALL THREE STAGES ARE BUILT** — A at SPEC.md §2.7.1, B at §2.1.2, C at
§2.5.3, with §2.9.5's copy retired along the way. Every number in §1 and §2 was
measured on this branch's own tree; §3 says where a figure is derived rather
than measured.

> **Read §1 as the state before the work, not as the tree.** Its tables were
> taken against a 19-sector blob and a 209-sector kernel, before `elendilon`
> merged the size pass. What the three stages actually did:
>
> | | before | after |
> |---|---:|---:|
> | stage 1's floor, kern_big | 140 KB | **115 KB** (derived from `MEMFIT`; 119 was a stale hand figure) |
> | …kern_small | 129 KB | **84 KB** (derived from `MEMFIT`; 107 was stale by 23 KB) |
> | `BOOT2_SECS` | 19 | **8** |
> | blob `int 13h`, 360/720/1.44 | 3/3/2 | **2/2/2** |
> | overlay pool | 7,168 (480 free) | 4,096 + **8,192** (2,929 free) |
>
> Both builds clear 128KB, which was the target — and **kern_big has since
> reached a desktop on an 86Box machine configured with 128KB**, which is the
> half the `RAMKB` knob cannot test: it moves the sector and the kernel still
> reads the real `int 12h`. §2.1.1 is the detour that cost the most to find and
> §2.3.1 is the one that cost a boot.

It exists because two questions turned out to be the same question. *"Why does
stage 1 refuse a machine the kernel fits on?"* and *"why is the splash so late,
and why does its bar start at a number that is already a lie?"* are both
answered by **where the boot chain's transient bytes live**, and moving them is
one change with three payments.

The target is **kern_small booting on the 128KB machine it is already asserted
to boot on**, and — on this branch, which `docs/plans/LAST-DROP-BYTES.md` has already
made 7,168 bytes smaller — **kern_big booting there too, with a usable heap.**

---

## 1. Three findings, measured

### 1.1 Stage 1 refuses machines the kernel fits on, by 31KB

`boot/boot.asm`'s `.nomem` (SPEC.md §2.7) compares the segment it would
relocate *into* against a build-time immediate:

```
    int 0x12
    mov cl, 6
    shl ax, cl                  ; KB*64 = one paragraph past the top
    sub ax, RELOC_ADJ           ; 0x07E0
    cmp ax, KERNEL_SEG + KERNEL_SECTORS*32 + BOOT_STACK/16
    jb  .nomem                  ; -> 'RAM', halt
```

Reduced, the floor is **`35 + ceil(KERNEL_SECTORS / 2)` KB**. The 35 is four
constants that do not sit next to each other in memory:

| term | paragraphs | bytes | what it is |
|---|---:|---:|---|
| `RELOC_ADJ` 0x07C0 | 1,984 | 31,744 | the sector keeps `org 0x7C00` after relocating |
| `RELOC_ADJ` 0x0020 | 32 | 512 | the sector's own body |
| `KERNEL_SEG` | 96 | 1,536 | IVT + BDA + the 0x500 page |
| `BOOT_STACK/16` | 128 | 2,048 | stage 1's stack |
| | **2,240** | **35,840** | |

**Only 3.5KB of that is memory anyone occupies.** The 31.5KB is an addressing
artifact: the sector relocates without re-originating, so its *segment base*
sits 0x7E00 below the top, and the compare bounds that base rather than the
sector. On the smallest machine it accepts it has asked for 2,048 bytes of
clearance above the kernel and received 32,256.

Two further over-counts ride in the same compare. `KERNEL_SECTORS` is the whole
of `kernel.bin`, whose first `BOOT2_PAD` bytes are the blob and never load at
`KERNEL_SEG` at all; and, in the other direction, `.lowbss`/`.vgabuf` are
`nobits`, so the compare's model of the kernel stops short of `KERN_END`. The
first two are slack, the third is not, and the first dominates.

**What it costs, both builds, read out of the assembled sectors:**

| | sectors | `cmp ax,` | stage 1 refuses below | `MIN_RAM_KB` |
|---|---:|---:|---:|---:|
| kern_big (this branch) | 209 | `0x1B00` | **140 KB** | 196 |
| kern_small (elendilon) | 193 | `0x1900` | **132 KB** | 128 |

The kern_small row is the finding. **Guard 5 asserts kern_small boots on 128KB
— with 13,824 bytes to spare — and stage 1 halts with `RAM` at anything under
132.** The kernel is not too big; the gate is wrong, and has been since the
gate and the constant were last true at the same time.

### 1.2 Nineteen sectors are read before the first splash pixel

SPEC.md §2.9.12 took `BOOT2_SECS` 13 → 19 to make room for
`docs/plans/LAST-DROP-BYTES.md`'s register, and booked the cost honestly: one extra
`int 13h` on the two 9-sector geometries, ~144 ms of in-run sectors on the
release one, all of it pre-splash. §2.9.6 had already booked the same kind of
cost when the overlay first joined the blob — *"seven more sectors before the
first splash pixel, about 170 ms"*.

Those costs are real and they are all paid **for bytes the splash does not
need**. `.ovl` is 6,688 of the blob's 9,728; not one byte of it runs before
`kmain`. Stage 1 reads it, at the track bound, onto a blank screen, and none of
it is counted in the bar that SPEC.md §15.3 went to some trouble to make
honest.

Measured with the tree's own instrument, off the images `make` built
(`tests/unit/t_blobruns.py --sectors N`):

| `BOOT2_SECS` | 360KB | 720KB | 1.44MB |
|---|---:|---:|---:|
| **19 (today)** | **3** | **3** | **2** |
| 7 … 13 | 2 | 2 | 2 |
| 6 | **1** | 2 | 2 |

### 1.3 The overlay's pool is nearly full

`.ovl`'s ceiling is `BOOT2_PAD - OVL_AT` = 9,728 − 2,560 = **7,168**, and `.ovl`
is **6,688**. `docs/plans/LAST-DROP-BYTES.md` puts what is left at **~416 bytes** on
the tightest shipped-adjacent arm, and records that the next claim above 21
sectors is *not free*.

So the branch that just proved `.ovl` is the most valuable section in the tree —
`KERN_SIZE` −7,168, the 64KB segment from 2,432 bytes of headroom to 7,214 — has
almost no `.ovl` left to spend, and the only way to buy more is to make every
boot slower on two geometries.

---

## 2. The proposal

**`.ovl` does not shrink and does not stop existing. It splits by lifetime, and
only the half that must outlive the first mount rides in the blob.**

The other half goes back to the FAT window, where SPEC.md §2.5 originally put
it — riding the kernel's own contiguous read, on `read_run`'s cylinder bound
(SPEC.md §18.91.1), inside the bar's denominator, and costing no resident byte
because those bytes are forfeit at the mount.

### 2.1 Stage A — the compare (independent, two lines)

Stage 1 cannot compute `HEAP_SEG`; that is why it uses a proxy. But the Makefile
already injects measured constants into that sector — `KERNEL_SECTORS`, `KSIG`,
`BLOBSUM` — and builds it *after* the kernel, so it can be told the real thing:

```
    ; ...no `sub ax, RELOC_ADJ` on the TEST path; AX stays KB*64
    cmp ax, HEAP_PARA + BOOT2_SECS*32 + (BOOT_SECT + BOOT_STACK)/16
    jb  .nomem
```

That is the exact condition: the blob, once stage 2 has copied itself to
`HEAP_SEG` (SPEC.md §2.9.5), must sit below stage 1's live sector and stack.
`RELOC_ADJ` stays where it belongs, on the relocation.

**This stage alone delivers the 128KB goal for kern_small** and is testable on
its own with `make RAMKB=<n>`. It should land first, separately, for that
reason.

#### 2.1.1 BUILT — and the 31KB was not all surplus

Stage A is done (SPEC.md §2.7.1). One thing in §2.1 above was wrong and the
correction is the interesting part.

The bound needs **two** blob lengths, not one. Stage 2 executes out of the
address stage 1 read it to while it `rep movsw`s itself down to `HEAP_SEG`, so
the far jump at the end of that copy has to survive being copied over. It is
overwritten at copy step `(load − HEAP_SEG*16) + B2_TAIL`, and the machine then
executes `.ovl` as code, resets, reboots and does it again — a loop whose only
symptom is `Disk error`, when the retries eventually lose a seek, from a sector
that has no idea why.

`boot/boot2.asm` said so and nobody read it that way: *"below the top of RAM by
stage 1's own .nomem refusal, **which is stricter than we need by 21KB**"*.
That margin was **load-bearing**. `RELOC_ADJ` is 31,744 bytes — more than a
second `BOOT2_PAD` — so the copy was always clear of itself by accident.

So the floor is `HEAP_PARA + ceil((2*BOOT2_PAD + 2,560 − B2_TAIL) / 16)`, and
`B2_TAIL` (`boot2_entry.reloc_tail`) is injected beside `HEAP_PARA`. Measured
transition on MartyPC before the fix: 128KB reset-looped, 132KB booted —
predicted at 132 to the kilobyte by the arithmetic above, which is what
identified the mechanism.

**And then it stopped costing anything, because the copy went.** The fix is
not to work around the overlap but to remove it: stage 1 knows `HEAP_PARA`
now, so it reads the blob straight to the heap's floor and stage 2 never moves.
That deletes the `rep movsw`, `STAGE2_ADJ`, `.reloc_tail`, `B2_TAIL`, the
computed far jump (a plain `jmp HEAP_PARA:0`, the segment being a constant)
and the second `BOOT2_PAD` — 59 lines net out of the two boot files, and
kern_big's floor 132 → **125KB**.

**Stage A is what made it possible**, which is worth stating because it reads
backwards: `boot2.asm` says the copy exists because *"HEAP_SEG falls out of the
kernel's section sizes and the sector has none of them"*. The sector has them
from the moment §2.7.1 injects `HEAP_PARA`, so the copy's whole reason expired
in the commit that introduced its cost. It is done with stage C rather than as
its own change, because it is the same subject: where the boot chain's
transients live.

**Both builds clear 128KB now** — kern_small with 28,672 bytes of arena,
kern_big with 15,872, which is the figure §2.9.6 already treats as the working
case. The stretch goal in §3 is met.

### 2.2 Stage B — the `.lowbss` reorder (independent, no code)

The FAT window is 4,608 bytes and this branch's `.ovl` is 6,688, so the window
alone no longer holds it. It does not have to. Measured off the listing, this
branch's `.lowbss`, bottom to top:

```
    0   696  vid_rowtab      live from vid_init, MARK 7
  696   256  gfx_pairtab0
  952   256  gfx_pairtab1
 1208    12  vga_pr_*
 1222   760  font_glyphs     written by ovl_font_init, MARK 13
 1982     8  font_zero
 1990    58  (align pad to 256)
 2048  2688  sch_stacks      task slots 1..MAX_TASKS-1
 4736   128  evq_buf
 4864   320  mem_tab         written by mem_init, MARK 12
 5184     4  mem_fptr
 5188    84  menu_bar
 5272   160  app_tmr_pool
 5432    80  app_ball_pool
 5512  1024  disk_dir        "ALWAYS exactly a mount snapshot"
 6536  2048  disk_icons      "fully rewritten every mount"
 8584   512  dsk_secbuf      "drv_boot's very first mount reads it"
       9,096 total, in a 10,240 rung
```

**The three mount-owned buffers are already one contiguous 3,584-byte block.**
They are simply at the wrong end. Move that block to the bottom of `.lowbss`,
immediately above `FAT_SEG`, and the dead-at-boot window becomes:

```
  FAT window   4,608
  disk_dir     1,024
  disk_icons   2,048
  dsk_secbuf     512
               -----
               8,192   contiguous, 512-aligned by construction
```

`.lowbss` is `nobits`, so this costs nothing on disk and nothing in RAM — same
bytes, same rung, same total. It is a reordering of declarations.

`sch_stacks` is a further 2,688 if it is provably dead before `drv_boot`, but it
sits between `font_glyphs` and `mem_tab` and both of those are live, so taking
it is a second reorder. **It is headroom this plan does not spend** and does not
need; §6.2 is the question that would have to be answered first.

#### 2.2.1 BUILT — and it is one include line

Stage B is done (SPEC.md §2.1.2). `kernel/dskwin.inc` holds the three buffers
and the four constants that size them, and is the **first file `kernel.asm`
includes**; `disk.inc` keeps everything else about a listing. Measured, both
builds: the window is at `.lowbss` +0/+1024/+3072, `.lowbss` totals 9,096 as
before, and the ladder is byte-for-byte unchanged —

```
KERNEL 0x0060  COLD 0x0ec0  FAT 0x1840  LOW 0x1960  VGABUF 0x1be0  HEAP 0x1c20
```

so the dead-at-boot region is `FAT_SEG` → `LOW_SEG + 3,584` = **8,192 bytes,
512-aligned**, against the 5,100–5,800 §2.3 needs. It cost nothing: `.lowbss`
is `nobits`, the rung is the size it was, and no address any code names moved.

**A section of its own was the obvious shape and is wrong.** These are reached
through **SS** with the rest of `.lowbss`, so `.mntwin` with a `vstart` of its
own would have been a code change at every access — in return for a placement
one include line already gives. What that costs instead is that the placement
is *invisible*: no RAM moves if it slides, no address changes, the kernel
boots, and only stage C finds out by writing the overlay over `vid_rowtab`.
`tests/unit/t_lowwin.py` is the row that exists for exactly that, and its
header says so at length.

Two things confirmed while doing it, both cheap and both worth having on the
record: none of the pre-mount disk entries (`dsk_boot_from_x`,
`dsk_flop_add_x`, `dsk_dpt_init_x`) references the window, so §2.1.2's
liveness claim is grounded rather than inherited from the buffers' comments;
and `tools/kernsize.py`'s theme table refused the new file until it was placed,
which is how 3,584 bytes of `.lowbss` moved from `disk.inc`'s row to
`dskwin.inc`'s without a byte moving in the machine.

### 2.3 Stage C — the split (the one with a silent failure mode)

Everything reached only before `drv_boot`'s first mount goes to the FAT window.
Everything reached after it stays in the blob.

kmain's order settles most of it. Every `OVLGATE` except the ones inside
`drv_boot` runs before it: `ovl_cpu_detect` (MARK 1), `ovl_xm_sniff` (2),
`ovl_clk_init` (6), `ovl_font_init` (13), `mouse_init` (18), `ovl_desk_init`
(20), `drv_snd_sniff` (25), `ovl_snd_init` (26), then `drv_boot` at 28.

**On this branch the split is much harder than on `elendilon`, and that is worth
stating plainly.** `docs/plans/LAST-DROP-BYTES.md`'s work moved `drv_boot_x` itself
into `.ovl`, along with `files_init_x`, `mem_init_x`, `sched_init`, `wm_init`,
`mouse_init`, `menu_init`, `inst_init`, `dsk_boot_from_x`, `dock_init`,
`drv_init_x`, `loader_init_x` and `drv_svc_clear_all`. So **the overlay is now
the thing running the mount**: put it back in the FAT window unchanged and
`drv_boot` overwrites itself mid-execution. That is exactly SPEC.md §2.5.1's
limit, no longer incidental.

Measured, the two branches for comparison:

| | `.ovl` total | must stay resident |
|---|---:|---:|
| `elendilon` | 3,969 | **~430** — the `ovl_spl_msg_*` / `spl_m*` family (426) + `ovl_cfg_load`'s stub (4) |
| this branch | 6,688 | **~890–1,560** — that, plus `drv_boot_x` and its post-mount callees |

The `elendilon` number is exact. **The range on this branch is the plan's first
task, not its conclusion** — §6.1.

One place the split is not clean either way: the splash message family
**spans** the boundary. `ovl_spl_msg_mouse` and `ovl_spl_msg_fdd` are
pre-mount, `ovl_spl_msg_cfg`, `ovl_spl_msg_drv` and `ovl_spl_msg_boot` are
post. They share `spl_mput` / `spl_mcs` / `spl_mds` / `spl_mnum`, so the whole
426-byte `vidsel.inc` block goes resident and the pre-mount callers far-call
into the blob. That is free — the blob is resident from stage 2's first
instruction — but it must be a decision rather than a discovery.

---

## 3. What it buys

Derived from §1 and §2's measurements; the input ranges are the ones §6.1
closes.

With the post-mount half at 890–1,560 bytes and `.boot2` at 2,460 under an
unchanged `OVL_AT` of 2,560, the blob lands at **7–9 sectors** (3,584–4,608)
against today's 19.

| | today | proposed |
|---|---:|---:|
| `BOOT2_SECS` | 19 | 7–9 |
| blob `int 13h`, 360 / 720 / 1.44 | 3 / 3 / 2 | **2 / 2 / 2** |
| sectors read before the first splash pixel | 19 | 7–9 |
| `.ovl` pool | 7,168 (480 free) | 8,192 pre-mount + ~1,000–2,000 post-mount |
| true minimum RAM, kern_big | 126,976 B (124 KB) | 120,832–121,856 B (**118–119 KB**) |
| stage-1 enforced floor, kern_big | 140 KB | **132 KB** built (§2.1.1); 125 with an overlap-proof relocation |

Four things fall out:

1. **kern_big boots on 128KB with a usable heap** — *not delivered by stage A
   alone; see §2.1.1. Its floor is 132KB until the relocation or the blob
   changes.* `HEAP_SEG` is 114,688 on
   this branch, so a 128KB machine has **16,384 bytes** of arena, of which
   11,776–12,800 are free while the blob stands. That lands on SPEC.md
   §2.9.6's own working figure — *"a 128KB machine's arena is 12KB"* — where
   the same machine on `elendilon` gets 9,216 and 2,560, which is not a machine
   anyone can use. **This is the argument for doing it on this branch and not
   the other one.**
2. **One `int 13h` back on both 9-sector geometries**, plus 10–12 fewer in-run
   sectors on all three: ~240–290 ms at PERFORMANCE.md Part 2's ~24 ms a
   sector, all of it off the blank screen. §2.9.12's and §2.9.6's booked costs,
   recovered.
3. **`.ovl`'s pool roughly doubles**, which is what `docs/plans/LAST-DROP-BYTES.md`'s
   remaining rows and every future boot-only body need — bought by moving bytes
   rather than by making every boot slower.
4. **The bar tells the truth about more of the boot**, because the overlay's
   sectors are inside the kernel's read and `dsk_xfer` calls `spl_step` once
   per sector (SPEC.md §15.3).

**The file does not grow.** New blob ~4,096 + image rung 58,368 + cold 38,823 =
101,287; the FAT position in the file is `BOOT2_PAD + (FAT_SEG − KERNEL_SEG)*16`
= 101,376, an 89-byte gap, then the pre-mount overlay — 209 sectors, exactly
what this branch is today.

#### 2.3.1 BUILT — and two of the three rules were written by a defect

Stage C is done (SPEC.md §2.5.3). 27 labels stayed in the blob, 77 went to the
window; `.ovl` is 1,425 bytes and `.ovlw` 5,263, of the same 6,688. The eleven
sectors that left the blob are in the kernel's own read now — cylinder-bounded,
and inside the bar.

**The reachability walk in §6.1 was necessary and not sufficient**, and that is
the finding worth keeping. Seeding from `drv_boot_x` and the splash family
closes the set under *near call*, which is what decides whether two bodies can
share a half. It says nothing about **when kmain calls a body**, and that is
the other half of the question: `xm_boot_x` is reached from no overlay body at
all, is boot-only, and was registered as such correctly — and kmain calls it on
the line *after* `drv_boot`. The split put it in the half that is a FAT table
by then. The machine mounted A:, far-called into that table, executed it, and
unwound its own stack until `sch_switch`'s canary caught the wreckage several
thousand instructions later, with nothing naming the overlay.

So the seed set is *the closure under near call, plus every overlay call site
at or after the mount* — and that second clause is now `os88ovlchk` rule 2e
rather than a paragraph, because it is a property of `kmain`'s line order and
nothing else in the tree was watching it.

Rule 2d has the same shape: `dsk_flop_add: OVLGATE1 dsk_flop_add_x` survived
the sweep that converted twenty-three other sites because the macro shared its
line with a label, and a blob pointer carrying a window offset is a spin at
`HEAP_SEG:36D2` with `SP` 53,482 bytes past task 0's stack. Both rules were
re-verified by reintroducing the defect and watching them fire.

**What §5's risk list got right and wrong.** The silent failure mode was where
it said, and the mechanical check was worth writing first. What it did not
anticipate is that the *registry was already wrong* the moment the deadline
split — 26 entries were re-audited and one was answering a question that no
longer existed.

---

## 4. Refusals

**Do not do stage C without stage B.** `.ovl` is 6,688 and the FAT window is
4,608. Splitting alone leaves the pre-mount half ~500–1,200 bytes over, and the
tempting fix — grow `FAT_PARA` — spends resident RAM on every machine to hold
code that is dead by the desktop, which is the exact trade `.ovl` exists to
avoid.

**Do not target kern_big on 128KB on `elendilon`.** 9,216 bytes of heap, 2,560
free during boot. It loads; it is not a machine. Measured before this plan
existed and recorded here so nobody re-derives it.

**Do not reuse the conservatism as a safety margin somewhere else.** Guard 5c is
currently written *against* stage 1's conservative bound, so stage A turns it
into an identity — it stops being an independent check. Losing a guard is a
real cost of stage A and the plan should say what replaces it (§6.3), not
quietly bank the win.

**Do not take `OVL_AT` down to fit a smaller blob.** SPEC.md §2.9.12 records
that `OVL_AT` moves for nothing and that neither half should have to argue for
its budget. `.boot2` is 2,460 of 2,560 and the two assertions at the foot of
`kernel.asm` still say which half ran out.

---

## 5. Risks

**The silent one, and it is stage C's.** A body in `.ovl` is correct only while
every path to it runs before the bytes are forfeit. `tests/ovlrefs.txt` already
registers every reference into `.ovl` and asks *"what guarantees this runs
before `spl_finish`?"* — stage C makes that question have **two different
answers** depending on which half a symbol lands in: before `spl_finish` for the
blob half, before **the first mount** for the FAT half. `tools/os88ovlchk.py`
must be taught the two halves before a line of stage C is written, or the
failure mode is a routine that works until the machine is busy enough — no
crash, no message, and a screenshot of a working desktop.

**A resting value that is no longer zero.** `.lowbss` is `nobits`, so its bytes
are the image's own padding — zero — and `tests/unit/t_bsssentinel.py` exists
because a byte whose resting value is not zero cannot live there. After stage B
the overlay's *code* lands on `disk_dir`/`disk_icons`/`dsk_secbuf` until the
mount fills them. Anything that reads those before the mount currently reads
zeros and would then read overlay code. The gate does not cover `.lowbss`
today; extending it is stage B's price.

**Two far-pointer pairs again.** SPEC.md §2.9.6 deleted `ovl_fp`/`ovl_fseg` and
"the two-call-kinds rule with its one-line boundary", and §2.5.1 records that
getting them the wrong way round was **silent in both directions**. Stage C
brings that back by construction. It is affordable only because it is
mechanically checked; see above.

**`SPLSTARS=1` needed re-pricing, and has been re-priced.** It carried its own
`BOOT2_SECS_STARS` = 20 when this was written and its `.boot2` was 2,824
against a 2,560 `OVL_AT`. **SPEC.md §15.3.8.5.1 took that arm to 2,568 and the
whole blob to 3,989 of 4,096**, so there is one blob length and one `OVL_AT`
(2,624) — which is exactly the "changes shape entirely" this paragraph
predicted, arriving from the other direction. `tests/unit/t_buildmatrix.py` is
still what asks.

---

## 6. What this plan does not settle

### 6.1 The exact post-mount set

§2.3's 890–1,560 is a bracket taken off label offsets in the NASM listing, and
the listing cannot separate co-located labels in `driver.inc`'s tail. The plan's
**first task** is a reverse-reachability walk from `drv_boot_x` — the same
method `docs/plans/LAST-DROP-BYTES.md` used, and for the same reason: *"it was nearly
disqualified wholesale by an edge that turned out not to reach it, and the
register that nearly disqualified it had also silently omitted 421 bytes."*
`BOOT2_SECS` falls out of that number and nothing before it should be treated as
final.

### 6.2 Whether `sch_stacks` is dead before `drv_boot`

2,688 bytes of further window if it is. Task 0's stack is in the rung above, not
here, and slots 1..`MAX_TASKS-1` are what this holds — but "no task is created
during boot" is an assertion nobody in this tree has had to make yet, and
`sched_init` moving into `.ovl` on this branch is a reason to check rather than
assume.

### 6.3 What replaces guard 5c

Stage A makes it an identity. The condition it proves is still worth proving at
build time; it needs restating against something stage 1 no longer duplicates.

### 6.4 `FLAT_PAYLOAD`

`rdiag`, `comscan`, `lptlink` and `dosstub` boot through this sector with no
`HEAP_SEG` to be told about. On the naive form of §2.1 the immediate goes
**negative** for a small payload and wraps unsigned, refusing every machine — on
the four images whose entire job is diagnosing a machine that will not boot. The
two payload kinds need different expressions and the diagnostic ones need a row
that proves they still boot.

---

## 7. Test rows

Existing rows this touches: `t_blobruns.py` (per-geometry ratchet — stage C
moves it), `ovlchk` + `tests/ovlrefs.txt` (stage C must extend both),
`bsssentinel` (stage B must extend), `t_buildmatrix.py` (`SPLSTARS`),
`kernresident`, `dljunk`.

New rows the stages need:

| stage | row | asserts |
|---|---|---|
| A | `RAMKB=` A/B per build | the new floor boots and one KB below it prints `RAM` — the shape `tests/dljunk.py` already uses |
| A | `FLAT_PAYLOAD` floor | each diagnostic image still boots on a small machine |
| B | `.lowbss` order | the three mount-owned buffers are contiguous and bottom-most |
| B | `bsssentinel` extension | no `.lowbss` byte is read before the mount on the overlay's footprint |
| C | `.ovl` half residency | every symbol is in the half its callers permit; a new reference names which |

`make test-full` remains the pre-merge gate, and it is the only thing that
builds `kern_small` — which is stage A's whole point, so **the kern_small arm
is not optional on this one.**
