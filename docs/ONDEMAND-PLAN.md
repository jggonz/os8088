# On-demand kernel modules

**Research document, not a contract.** SPEC.md is the binding contract for
what the kernel *is*; this is the study of a mechanism nobody has built yet.
Every figure below was measured on this tree at `9ff3bd3` with the method in
§3.1, and the ones that are derived rather than measured say so.

The ask, in the requester's words:

> Investigate creating a new kernel concept: on demand drivers. This can
> likely reuse most of the existing driver architecture. One example of a
> usecase is the "Disk Format" command — rarely used, and could be loaded from
> disk, ran, and then discarded. The goal is to shrink the kernel and the
> kernel's memory usage. Find and recommend any other potential usecases in
> the kernel.

---

## 0. The verdict, up front

**The premise is right, the mechanism is 95% built already, and it is not the
driver architecture — it is `.cold`.**

Four findings, in the order they change what you would do:

1. **`.cold` is an on-demand image in everything but where it lives.** It is
   assembled at `vstart=0`, it contains **zero data directives** (proved in
   §3.2, not asserted), it runs with `DS = KERNEL_SEG`, and every call it
   makes out of itself already goes through one of **107 `cw_*` far shims**.
   Nothing in those 23,200 bytes cares that it is at `COLD_SEG`. Point the
   thunk at a heap claim instead and the same bytes run unchanged.

2. **The conversion costs `.text` nothing, which is the guard that cannot be
   raised.** The `.text` thunk (`cp_paint: call COLD_SEG:cpf_cp_paint`) does
   not change at all — the *cold* thunk becomes the load-and-dispatch stub, and
   cold code is always resident. `kern_big` has **438 bytes** left against
   `KERN_CODE_MAX` and no conversation can raise it; this mechanism does not
   spend one of them.

3. **The three best candidates are the Standard File dialog, Cut/Copy/Paste and
   the Control Panel — and they are never in use at the same time.** Measured,
   severing all three takes `kern_big` from **102,912 → 92,160** and
   `kern_small` from **97,280 → 86,528**; the realistic code-only conversion is
   **~8.7 KB** of that. `kern_small` today stands at **exactly zero bytes of
   spare against its guard**, so this is not an optimisation, it is the next
   place the small build's headroom comes from. The Control Panel is the
   biggest *mechanical* fit of the three and goes **last**, for a reason that is
   not mechanical at all (§4.1): it is the window `drv_notice` opens when a
   driver will not load, and two of the seven ways a driver fails are ways this
   would fail too.

4. **Disk Format — the motivating example — is the *worst* candidate in the
   kernel, for two independent reasons**, and §5 is the working. It is worth
   reading before any code is written, because the feature that prompted the
   idea is the one that should go last, or not at all.

**What this is not.** It is not a second driver class, it is not an API slot,
and it is not a package. §7.1 says why each of those three is the wrong shape,
and two of them were already rejected once for the same reasons in
docs/HDD-SPLIT-PLAN.md §5.0.

---

## 1. Where the kernel already stands

`kernsize.py`, this tree's own instrument, on the shipped build:

```
kernsize[big]: sections   text 59,394  bss 5,704  cold 23,200  lowbss 7,762  ovl 3,138
kernsize[big]: footprint  KERN_SIZE 102,912 of KERN_BUDGET 104,960 -> 2,048 spare (4 steps)
kernsize[big]: segment    .text+.bss 65,098 of KERN_CODE_MAX 65,536 -> 438 left
kernsize[big]: ladder     HEAP 0x1980 = 102.0 KB
```

and the small build, which is the one this is for:

```
kern_small:    KERN_SIZE 97,280 of KERN_BUDGET 97,280 -> 0 spare
```

**Zero.** Not four steps, not one — the next byte added to `kern_small`
anywhere fails to build. docs/KERNEL-MEMORY.md's move 21 was the last 1 KB and
its terms were move 5's: headroom for ordinary growth, not an invitation. There
is no ordinary growth left in it.

On the machine the floor is written for — 128 KB — the heap is what is left
above the kernel: **26.0 KB** under `kern_big` and **31.5 KB** under
`kern_small`. A package region, a Disk window's 3 KB listing cache, a FAT
window and a copy buffer all come out of that.

### 1.1 The four levers that exist, and what each relieves

This is the table the rest of the document turns on, because three of the four
are routinely confused with each other and only one of them is new.

| lever | `KERN_CODE_MAX` (segment) | `KERN_BUDGET` (footprint) | cost |
|---|---|---|---|
| **`.ovl` boot overlay** (§2.5) | relieved | **relieved** — it lands in the FAT window and is overwritten by the first mount | run-once code only |
| **`.cold` cold segment** (§2.6) | relieved | **not relieved** — still resident | a far call per crossing |
| **out to a package** (§28's Task Manager) | relieved | relieved | a published ABI, an instance, a `.o88` |
| **on-demand module** *(this document)* | not spent | **relieved** | a disk read per use, and §8's traps |

The row that matters is the second one. **Moving a module cold to fix a
footprint overrun is a no-op that looks like a fix** — docs/KERNEL-MEMORY.md
says so already — and 23,200 bytes of this kernel have taken that route and are
still resident. This mechanism is what finishes the journey for the part of
that which is *rarely used*.

### 1.2 The precedent is already in the tree

`HDD.DRV` loads `HDDTOOL.DRV` — an 11 KB second image read into a heap claim
and far-called — and frees it at detach (SPEC.md §52.11,
docs/HDD-SPLIT-PLAN.md). It cost **no kernel byte**, it works, and it was
verified end to end under QEMU.

Three things carry over from it and one does not. What carries: the file is a
`.DRV` so `os88disk.py`'s `sys_attr` gives it hidden+system+read-only *by
extension* with no tool-chain change (§19.6), the installer's "every `*.DRV`"
copy picks it up for free, and `ld_check_hdr` refuses it so it can never be
double-clicked. What does not carry is the **shape**: `HDDTOOL.DRV` is a
package — its own `CS` *and* `DS`, its own dispatcher, reaching the kernel
through `OSAPI_*` far calls and its resident half through a verb table. A
kernel module wants none of that, and §2 is why.

---

## 2. The finding: `.cold` is the mechanism, minus where it lives

`section .cold start=COLD_START vstart=0`.

Three properties follow, and all three were checked rather than assumed:

- **It is pure code.** A scan of every `.cold` block in `kernel/` for `db`,
  `dw`, `dd`, `resb`, `resw`, `times` and `incbin` returns **0 directives**.
  Cold modules put their data back in `.text` on purpose — `diskw.inc` even
  labels the switch *"DATA, so back to the kernel segment"*. So a cold module's
  data already lives somewhere a moved image can still reach.

- **It runs with `DS = KERNEL_SEG`.** Every kernel variable, every string,
  every table is reached by the same absolute offset it is reached by from
  `.text`. **A module that moves does not have to re-express one data access.**
  This is the whole difference from a package, and it is what makes the
  conversion a build change rather than a rewrite.

- **It never near-calls out of itself.** 107 `cw_*` shims in `.text` are its
  entire outbound surface, each an absolute `call KERNEL_SEG:cw_x`, and
  `tools/os88ovlchk.py` fails the build if a near call ever crosses. The
  segment those shims name is a constant.

Put together: **`.cold` code is already position-independent at paragraph
granularity.** Load it at any paragraph, set `CS` to it, leave `DS` alone, and
it runs. The only thing tying it to `COLD_SEG` is the constant in the 40-odd
inbound thunks — `call COLD_SEG:cpf_cp_paint` — and that constant is one word.

That is the finding. Everything below is what to do with it.

---

## 3. The candidates, measured

### 3.1 Method

A scratch copy of `kernel/` with one `%include` removed, then assembled with
the real flags (`-f bin -w+error -DKERNSIZE -DKERN_BIG`) and the real include
paths. **nasm is the oracle**: anything the rest of the kernel still needs
shows up as an undefined symbol, and the loop stubs each one and re-assembles
until it converges. This is docs/HDD-SPLIT-PLAN.md §4.1's severance, automated,
and the stub count *is* the seam width — it is the number of entry points a
real conversion has to route.

The sizes come from `kernel.asm`'s own `ks:` line under `-DKERNSIZE`, which is
where `kernsize.py` gets them, so there is no second opinion about how
`KIMG_PARA` rounds. Stubs cost one byte each and are left in the figures, so
every saving below is understated by at most a few dozen bytes.

### 3.2 The table

Δ against `kern_big` at 102,912. **`Δksize` is the footprint** — what the
machine gets back — and it moves in 512-byte rungs, which is why it is not the
sum of the columns beside it.

| candidate | Δ`.text` | Δ`.cold` | Δ`.bss` | **Δksize** | seam | how often used |
|---|---:|---:|---:|---:|---:|---|
| **`fdlg.inc`** — Standard File dialog (§38) | −223 | **−3,907** | −106 | **−4,608** | 18 | seconds at a time, modal |
| **`ctrl.inc`** — Control Panel (§31) | −672 | **−3,204** | 0 | **−4,096** | 15 | rarely, minutes apart |
| `assoc.inc` — file associations (§54) | **−2,809** | 0 | −43 | −3,072 | 11 | every document open |
| **`filecp.inc`** — Cut/Copy/Paste (§22.3–22.5) | 0 | **−2,134** | −135 | **−2,560** | 12 | per file operation |
| *floppy formatter* (§18.96 + §22.12) | −342 | −1,357 | −1 | −2,048 | (exact) | almost never |
| `icons.inc` — the icon renderer (§10) | −1,570 | 0 | −34 | −1,536 | 7 | every desktop paint |
| `xmem.inc` — memory above 1MB (§41) | −1,040 | 0 | −124 | −1,536 | 9 | rarely |
| `fsx.inc` — fullscreen exclusive (§53) | −916 | 0 | −9 | −1,024 | 4 | per frame, in a game |
| `loader.inc` — the package loader (§21) | 0 | −776 | −58 | −1,024 | 9 | every launch |
| `clip.inc` — the clipboard (§55) | −193 | 0 | −6 | −512 | 3 | per copy |

The floppy formatter's row is not a severance — it is the exact delta of its
existing `%ifndef KERN_SMALL` guards, so it is the one figure here that is a
fact about the shipped build rather than about a scratch one.

### 3.3 What "severed" over-states, and by how much

**A severance removes the module's data too, and a real conversion does not.**
Only `.cold` leaves; the `.text` bytes are the module's tables and strings and
they stay resident, reached through `DS` exactly as now. So read the
**Δ`.cold`** column, not Δksize, for what actually moves:

| | `.cold` today | after | rung | **footprint saved** |
|---|---:|---:|---:|---:|
| Control Panel out | 23,200 | 19,996 | 40 × 512 | **3,072** |
| File dialog out | 23,200 | 19,293 | 38 × 512 | **4,096** |
| Cut/Copy/Paste out | 23,200 | 21,066 | 42 × 512 | **2,048** |
| **all three** | 23,200 | **13,955** | 28 × 512 | **9,216** |

less the dispatcher and the per-entry stubs that go back into `.cold`
(~150 bytes each, 19 entry points across the three), which rounds the answer to
**~8,704 bytes — seventeen 512-byte steps.** That figure is *derived* from
measured section sizes; it is not itself measured, and it will not be until
something is built.

For the machine at the floor, that is a heap of **31.5 KB → 40.2 KB, +27%**.

### 3.4 The peak is BETTER, which is the opposite of the HDD split

docs/HDD-SPLIT-PLAN.md §10 had to report that the peak got 3 KB *worse*,
because the tool image duplicated helpers it could no longer near-call. Nothing
is duplicated here — the bytes move, they are not copied — so the only overhead
is `mem_claim`'s rounding to whole KB:

| | today | after, idle | after, Control Panel open |
|---|---:|---:|---:|
| kernel footprint (small) | 96.5 KB | 87.8 KB | 87.8 KB |
| loaded module | — | — | 4 KB |
| **total** | **96.5 KB** | **87.8 KB** | **91.8 KB** |

**And the three share one slot.** The file dialog is modal (`[fdlg_win]` is
enforced at three call sites), the Control Panel is a window the user is
standing in front of, and a paste runs to completion inside one operation —
so the realistic concurrency is one, occasionally two. Three permanent
residents become one transient one. That is the argument, and it is stronger
than the byte count.

---

## 4. The recommendation

**Build the mechanism against the Standard File dialog first, then Cut/Copy/
Paste. Take the Control Panel only after reading §4.1, and Disk Format not at
all.**

- **`fdlg.inc` first.** The biggest single win at 3,907 bytes of `.cold`, the
  narrowest seam in the whole table — **8 symbols, 7 of them the `fdf_*` cold
  thunks** and the eighth one word (`fdlg_win`) — and it is the candidate with
  **no diagnostic role**, which §4.1 says is the property that matters most.
  It is behind a published slot (`OSAPI_FILE_DLG`, 0x0150), which is fine and
  is the point of §7.3: the *slot* stays, its body loads. `fdlg_open_x`
  already has a `.refuse` exit, so "could not load" needs no new contract, and
  it is modal — nothing else is clickable while it is up and it destroys its
  window on the way out, which is §8.5's easy case.

- **`filecp.inc` second**, and this one deserves a measurement before it is
  taken: a paste is *itself* disk work, so the load rides in front of an
  operation that already takes seconds, but a Cut/Copy/Paste is also far more
  frequent than the other two. It is the first candidate where "rarely used" is
  arguable rather than obvious.

- **`ctrl.inc` third, and conditionally.** On every mechanical measure it is
  the best candidate in the kernel: 15 symbols of seam of which 6 are already
  the `cpf_*` thunks and the other 9 are four words of state (`cp_sel`,
  `cp_dirty`, `cp_wdirty`, `cp_nst`), three data pointers (`cp_tpl`,
  `cp_sname`, `cp_s_dsdrv`) and two constants (`CP_ITIME`, `CP_IDRV`) that all
  stay resident in `.text` anyway; **zero `.bss`**; every entry point on the UI
  task with the lock held; nothing calling it in a loop. §4.1 is the one
  argument against it and it is not a mechanical one.

### 4.1 The Control Panel is the window you open when something is wrong

docs/KERNEL-MEMORY.md already says this, in the passage explaining why the
Task Manager was allowed to leave the kernel and be a package:

> The **Control Panel** — the window you want when a driver will not attach,
> and where `drv_notice` sends you — is cold and therefore still resident.

That is load-bearing and it argues against the recommendation this document
started with. `drv_boot` runs before the first paint, banks a failure in the
row, and `drv_notice` afterwards does `mov byte [cp_sel], CP_IDRV` /
`app_launch KIND_CTRL` — it opens the panel on its Drivers page so the machine
can *say* what happened. Two of the seven `DRVE_*` codes are precisely the
conditions under which an on-demand panel could not load either:

- **`DRVE_DISK`** — no readable system disk in A:. The panel's image is on
  that disk.
- **`DRVE_MEM`** — the heap could not fund the driver. It very likely cannot
  fund a 4 KB panel a moment later either.

So the machine that most needs to explain itself becomes the machine that
cannot. That is a **strictly worse** failure than today's, and it is not
hypothetical: `DRVE_DISK` is the ordinary state of a single-floppy machine
that has swapped to the apps disk, which is the calibration machine
(docs/FIELD-MACHINES.md).

It is survivable, and the tree has already built the survival. §59.6 moved the
Control Panel's own save verdict into the **menu bar** for exactly this
species of reason — a verdict that outlives the window it is about — and one
string per `DRVE_*` in a toast is the same answer applied one step earlier.
`drv_errstr` is already that table of strings, already bounded, already
written to name a *fact about the machine the user might act on*.

**But that is new work, on the diagnostic path, and it must be built before
`ctrl.inc` moves and not after.** Which is the whole reason the panel is third
here rather than first: the mechanism should be proved twice on features whose
failure costs the user a click, before it is pointed at the one whose failure
costs them the explanation.

Stop there. §6 is the survey of everything else and the answer for all of it is
no, for reasons that are worth having written down.

---

## 5. Disk Format: why the motivating example is the worst candidate

Two independent reasons, either of which is sufficient.

### 5.1 The disk you must read is not in the drive

SPEC.md §22.12 is explicit about what this feature is *for*:

> A floppy that reads and is not FAT12 shows `No os8088 disk (A:)` and nothing
> else — the window is a dead end with a perfectly good disk in it.
> **File ▸ Format Disk…** is the way out.

So at the moment the user reaches for it, the disk in the drive is **the one
about to be erased**, and on the calibration machine — one 360 KB floppy,
docs/FIELD-MACHINES.md — that is the only drive there is. `drv_load`'s own
path is `drv_vol_bank` → `drv_mounted` → read from the system volume →
`drv_vol_back`; here it would find a foreign disk and answer
`Need the system disk`, which is docs/HDD-SPLIT-PLAN.md §6.4's regression with
the convenience argument replaced by an impossibility one. Hard Disk Format
merely became *inconvenient* on a single-floppy machine; floppy Format becomes
**unreachable on exactly the machine and exactly the disk it exists for**.

The ways out, and none of them is cheap:

- **Load earlier.** There is no earlier. `fm_bar_gate` runs on the press that
  opens the menu, which is already after the swap.
- **A swap prompt** — *insert the system disk* → load → *insert the disk to
  format* → format. The kernel has no such prompt and building one is new UI,
  new modal state, and two extra swaps on the machine with the fewest drives.
- **Accept it on two-drive and installed machines only.** True — a hard-disk
  machine reads the module off C: and formatting A: is free — but that is the
  machine with 26 KB of heap to spare, not the one with 31.5.

### 5.2 The split has already removed it from the machine that needs it

`dskw_fmt_*` and its whole UI are inside `%ifndef KERN_SMALL`. **The 128 KB
machine does not have the formatter and has never paid for it.** What on-demand
loading would buy is 2,048 bytes on `kern_big` — the build with 4 steps of
spare, running on machines with hundreds of KB free.

That is the general rule this example teaches, and it is worth stating on its
own:

> **On-demand loading earns its keep on code that BOTH builds must keep.** Where
> a feature can simply be compiled out of the small build, the split is
> cheaper, simpler and has no failure mode. Where it cannot — because both
> machines want it, and both want it rarely — that is where this mechanism is
> the only lever left.

The Control Panel, the file dialog and Cut/Copy/Paste are all in both builds
and cannot leave either. That is precisely why they are the recommendation and
Format is not.

### 5.3 If it is wanted anyway

It is the one candidate whose engine and UI are already *separated by a build
flag*, so the conversion is mechanical and the risk is low. Take it **last**,
after the mechanism has shipped twice, and take it with §5.1's failure as an
accepted, documented refusal on single-floppy machines rather than as a bug to
be fixed later. `dskw_fmt_probe` runs at menu time and reads only, so the
refusal can at least be delivered before the confirmation is armed rather than
after the user has said yes.

---

## 6. Everything else, and why not

- **`assoc.inc` (3,072)** — the largest `.text` candidate, and the only one
  that would relieve `KERN_CODE_MAX`'s 438 remaining bytes. Rejected on
  frequency: it is on the path of **every document double-click**, which is the
  commonest way anything is launched. Worth converting to `.cold` on its own
  merits (that *does* relieve the segment guard), and that is a different
  document.
- **`icons.inc` (1,536)** — every desktop paint, every Disk window row, every
  dock tile. Not rare by any reading.
- **`loader.inc` (1,024)** — it is what loads things. A load path that must
  load itself is the one genuine circularity here.
- **`fsx.inc` (1,024)** — narrowest seam in the table (4 symbols) and
  tempting for it, but `fsx_wait` is the **frame clock**: a game calls it once
  per frame, and §53.2's whole argument is that the bracket has no jitter in
  it. An indirect far call per frame is affordable; a `mod_need` test per frame
  is the wrong shape of code to put there.
- **`xmem.inc` (1,536)** — genuinely rare, and already removed from
  `kern_small` (§41.11), so §5.2's rule applies exactly as it does to Format:
  the machine that needs the memory already does not pay.
- **`clip.inc` (512)** — one rung, three symbols. Too small to be worth a
  failure mode.
- **`splash.inc` (961 `.text`)** — worth a mention because it looks like a
  candidate and is not one: after boot the only live entry is `spl_step`, a
  compare and a `ret` called once per sector from `dsk_xfer` forever. The rest
  is dead weight, but it must be resident *within the image's opening
  `SPL_RESIDENT` sectors* because the boot sector ticks the bar while the
  kernel is still arriving, so it can be neither loaded on demand nor deferred.
  If those ~900 bytes are ever wanted back, the lever is `.ovl`, not this.
- **The RTC write paths** (`clk_at_write` 141, `clk_rp_write` 130,
  `clk_ns_write` 99 — 370 bytes) — only reachable from the Control Panel's
  Date/Time page, so they are **not a candidate of their own**: they are three
  routines that should simply move into whatever image the Control Panel ends
  up in. Noted here so the next person does not cost them separately.
- **`files.inc`'s 6,590 bytes of `.cold`** — the Disk window is Locator, and
  Locator is what the machine is when nothing else is running.

**And a shape worth recognising for next time.** The largest single routine in
the whole kernel is `osapi_table` at 944 bytes, and it is a table. The next is
a 258-byte icon. **There is no hot spot anywhere** — 82,594 bytes of code with
nothing over 200 bytes in it — which is SPEC.md §5.7's finding about `gfx_pixel`
in another register. The unit of on-demand loading therefore has to be a
*feature*, never a routine, and any proposal here that names a function rather
than a module is proposing to spend a disk read to save a hundred bytes.

---

## 7. The mechanism

### 7.1 What it is not

- **Not a `drv_tab` row.** A driver is *ticked*, has a class, a publication
  slot, a `SYSTEM.CFG` bit and a Control Panel line. None of that applies, and
  docs/HDD-SPLIT-PLAN.md §5.0 rejected it once already for the same reason.
- **Not a package.** A package owns its own `DS`, and owning its own `DS` is
  exactly what a kernel module must not do — it would have to re-express every
  one of its data accesses and re-reach the kernel through `OSAPI_*` instead of
  the `cw_*` shims that already exist. That is the difference between moving
  1,300 lines and rewriting them.
- **Not an API slot.** Nothing outside the kernel calls it. §20.8's rule 4 is
  not engaged, no `.o88` is invalidated, and `apps/os88api.inc` does not change.

### 7.2 What it is

**A `.cold` section that ships as a file instead of as part of `kernel.bin`.**

- **The image** is the module's `.cold` output, assembled at `vstart=0` exactly
  as now, with a small header on the front. It ships as `<NAME>.DRV` in the
  system volume's root, which buys hidden+system+read-only by extension, the
  installer's copy rule and `ld_check_hdr`'s refusal, all for free (§1.2).
- **The contract inside it is `.cold`'s, unchanged**: `CS` = the claim,
  `DS = KERNEL_SEG`, out through `cw_*`, near `ret`s, `tools/os88ovlchk.py`
  policing the boundary as it does today.
- **The claim** is `mem_claim_hi` with a purgeable tag (§7.5) — a module's base
  is its `CS` so it can never move, which is `mem_claim_hi`'s existing reason
  for existing.
- **The inbound thunk does not change in `.text`.** `cp_paint` stays
  `call COLD_SEG:cpf_cp_paint`. What changes is `cpf_cp_paint` — it becomes a
  cold stub that calls `mod_need` and then `call far [mod_ent + n]`. Cold code
  is always resident, so **the resident cost against `KERN_CODE_MAX` is zero.**

### 7.3 The header, and the one thing it must carry

A kernel module is not a package and does not need `call bp / retf` — the
kernel knows the entry offsets, because it built them. That is cheaper and it
is also a hazard: **a stale module file beside a newer kernel would far-call
into the wrong offset, silently.** So the image carries an entry *table*, and
a stamp:

```
  0   dw  0x384F          ; 'O','8'
  2   db  5               ; kernel module (a package is 3, a driver is 4)
  3   db  module id
  4   dw  the kernel's BUILD NUMBER      <- tools/buildnum.py, already in the tree
  6   dw  image size = the file's whole size
  8   dw  entry count
 10   dw  entry[0] .. entry[n-1]         ; offsets in this image
```

The build stamp is the load-bearing field. `buildnum.py` already produces a
number that moves on **every commit** (SPEC.md §14.2, and it is why the
`md5sum` shortcut in CLAUDE.md only works within one commit), so a module and
the kernel that can call it agree by construction and a mismatch refuses at
load. Version 5 keeps both `ld_check_hdr` (3) and `drv_check` (4) refusing it,
which is the two-independent-gates discipline §51.1 already uses.

### 7.4 The loader

`drv_load` minus everything that is about being a driver: no row, no class, no
`drv_publish`, no `DRVV_ATTACH`, no tier. What survives is the useful half, and
it is about 130 bytes today —

```
    drv_vol_bank                 ; the user may be anywhere (§51.5.2)
    drv_mounted                  ; is the system volume reachable?
    drv_find / dskw_stat         ; how big is it
    mem_claim_hi                 ; one claim, at the size the directory reported
    dskw_read                    ; the whole file, by name
    <header check>
    drv_vol_back                 ; preserves CF
```

— and it belongs in `.cold` beside the stubs that call it, not in `.text`.
`drv_vol_bank`/`drv_vol_back` are §51.5.2's bank-and-restore and their compare
makes the common case free.

### 7.5 Discard: shed, do not free

The obvious design frees the image when the feature closes. **Do not.** The
tree already has the better answer and it is §50.6's purgeable claim:

- tag the image `MEM_PG_LOW` — *"losing it costs a little I/O, or a visible
  pause"*, which is exactly what a re-read costs;
- a second visit to the Control Panel then costs **nothing** on a machine with
  room, and the image is the **first thing shed** on a machine without;
- `mem_claim`'s shed-and-retry (§50.6.2) means a package load that would have
  been refused now takes the Control Panel's image instead of failing —
  which is strictly better than either freeing eagerly or holding forever;
- and §50.6.1 places purgeable claims *inside the data arena*, below the lowest
  region base, so a cached module image cannot fragment the run a package needs.

**With one hard rule, which is §8.1.**

---

## 8. The traps

### 8.1 A module must not be purgeable while it is entered

`mem_claim`'s shed-and-retry fires on a refusal and takes the
lowest-priority cache. If the Control Panel's own image is a cache while
`cp_onclick` is running inside it — and `cp_onclick` reaches `drv_load`, which
reaches `mem_claim_hi` — the machine can shed the code it is standing on. It
would not fault; it would run whatever the next claim wrote there.

A one-byte nesting count per module, incremented by the stub before the far
call and decremented after, with the claim pinned while it is non-zero. The
counter must be per module and not a single flag, because a module can re-enter
itself through a callback.

### 8.2 The register the verb travels in

docs/HDD-SPLIT-PLAN.md §10 sprang this exact bug twice in one build: the verb
was passed in `BP`, which is the dispatcher's *address*, and then the stub
restored `BP` after kernel calls that spend it. Both were silent — nothing
faulted, nothing hung, nothing appeared on screen. Here there is no dispatcher
at all (§7.3), so the entry is an index into the header's table and `BP` is
free; but the general shape stands, and the stub is the one place in this
mechanism where a register contract is being invented rather than inherited.

### 8.3 The load happens under the gfx lock

Every candidate's entry points are called from the UI task with the lock held —
that is `W_PAINT`/`W_ONCLICK`'s contract. So the module load is disk I/O inside
a lock hold, which is **already what `drv_load` from a Drivers-page click
does**, and what `ui_tm_open` does for `TASKMGR.O88`. It is not new. What is
new is that it happens on the path of *opening a window*, where the user has
given no prior indication they are about to wait: derived from PERFORMANCE.md's
24 ms per 512 bytes delivered plus its isolated-access note (a first-sector read
is a **seek**, ~80 ms average and worse at the stroke's end), a 3.2 KB module is
**roughly a quarter to half a second**. That is an inference from two measured
figures and not itself a measurement; it belongs in a field run before anybody
quotes it.

§18.95's sector cache and §18.8.1's per-volume FAT window both work in its
favour on the second load, and §7.5's purgeable cache means there usually is no
second load.

### 8.4 A refusal is a normal path, and every caller needs one

No system disk, a full heap, a corrupt image, a build-stamp mismatch. §50's
rule already says refusal is normal and every claim in the tree has a fallback;
the work here is that **three features that could not previously fail now can**.
`fdlg_open_x` has `.refuse` already. The Control Panel's is `ui_dispatch`
declining to open the window and saying so — and §59.6 has already built the
place to say it, in the menu bar, after the panel's own window is gone.

The one that needs thought is `cp_flush_close`: the Control Panel writes
`SYSTEM.CFG` when it *closes* (§31.8), so the image must still be resident at
that moment. It will be — the close is a call into the module — but it means
the pin in §8.1 has to cover the whole open-to-close span for this module, not
just one entry. That is a per-module property, not a general one, and it is the
argument for the Control Panel keeping its image for as long as its window
exists rather than per call.

### 8.5 A window that outlives its image is a paint into freed memory

docs/HDD-SPLIT-PLAN.md §6.1 exactly. The Control Panel's window and the file
dialog's window both dispatch through `W_DISP`/`W_SEG`; if the image is shed
while a window still names it, the next paint runs freed memory. §8.4's pin
covers it for the Control Panel; the file dialog is modal and destroys its
window on the way out, which is the easier case. **This is the trap to test
first and it fails as a hang, not as an error.**

### 8.6 The build has to keep them in step

Three new obligations for `make`: the module images build from the same source
tree as the kernel, they carry its build number, and they land on **both**
shipped floppies and in the installer's copy set. A kernel that ships without
its modules is a machine whose Control Panel does not open — so
`tools/os88disk.py` should refuse an image whose kernel names a module the
volume does not carry, the way it already refuses an over-long root listing.

---

## 9. What this does not change

- **No API slot, no `.o88` invalidated, no package rebuilt.** §20.8 rule 4 is
  not engaged.
- **`KERN_CODE_MAX` is not spent** (§7.2), which is the guard with 438 bytes
  left and no way to raise it.
- **`.cold`'s contract is unchanged** — same `vstart`, same `DS`, same `cw_*`
  shims, same `os88ovlchk.py`.
- **No module's data moves**, so no string staging (§31.9's `DSV_CPNAME` trap)
  and no `cs:` overrides anywhere.
- **The kern_small/kern_big split is untouched** and stays the right answer for
  everything it can reach. This is for what it cannot.

---

## 10. Decisions for the owner

1. **Is the mechanism worth ~8.7 KB on both builds?** `kern_small` is at zero
   spare, so the alternative to finding room somewhere is a further budget move
   for the 128 KB machine — which docs/KERNEL-MEMORY.md says should be answered
   by a second build rather than a raise, and the second build already exists
   and has nothing left to give.
2. **Does the Control Panel move at all?** §4.1 is the case against, and it is
   an argument about diagnosis rather than about bytes: the panel is where
   `drv_notice` sends a machine whose driver would not load, and two of the
   seven reasons a driver fails are reasons the panel could not load either.
   Answering it needs one string per `DRVE_*` in a toast — §59.6's mechanism,
   one step earlier — built **before** the panel moves. Without that, the 3 KB
   is not worth it and the file dialog plus Cut/Copy/Paste are 6 KB on their
   own.
3. **Purgeable cache (§7.5), or free on close?** The cache is better on every
   axis except that it makes §8.1 mandatory rather than optional.
4. **`.DRV`, or a new extension?** `.DRV` buys the attributes, the copy rule
   and the loader's refusal for free, at the cost of a file that is not a
   driver being called one — docs/HDD-SPLIT-PLAN.md's decision 4, unresolved
   then and unresolved now, and this would be the second thing in that
   position.
5. **Is §5.1's refusal acceptable if Format is ever taken?** On a
   single-floppy machine, on-demand Format cannot work without a swap prompt
   the kernel does not have.

---

## 11. If it goes ahead

Six commits, each buildable and testable alone. Only the last two can break
anything.

1. **The header, the loader and the pin** — `mod_need`, `mod_pin`,
   `mod_release`, all in `.cold`, plus `tools/os88mod.py` to stamp an image.
   Nothing calls any of it. `kernel.bin` grows by a few hundred bytes of
   `.cold` and by **zero** bytes of `.text`; check that.
2. **Build `FDLG.DRV` as a second output of the same source**, and ship it on
   both floppies — while `fdlg.inc` is *still* compiled into the kernel. Two
   copies of the same bytes, one of them unreachable. The commit that proves
   the build works.
3. **Route `fdf_*` through `mod_need`**, still with the resident copy present
   as the fallback. Every file-dialog path now goes through the new mechanism
   on a machine where it cannot fail.
4. **Delete `fdlg.inc`'s `.cold` from the kernel image.** The first commit that
   can break something, and the one to A/B: same scripted session against both
   builds, framebuffer byte-identical, which is `make REDRAWFULL=1`'s discipline
   applied to a different question.
5. **`filecp.inc`**, same three steps compressed into one now the mechanism
   exists, and after the measurement §4 asks for.
6. **`ctrl.inc`**, and only behind §4.1's toast — which is a commit of its own,
   before this one, and is worth having whether or not the panel ever moves.

**How it gets tested.** `make marty` throughout — this is 8088 code, all three
adapters, and MartyPC's floppy turns (PERFORMANCE.md Sets 35/37) so the load
cost is within a measurement quantum of the iron rather than 30x fast. The
specific things to drive: open and close the Control Panel twice in one session
(the claim must not leak and the second open must be free); open it on a machine
with the heap deliberately filled, and check the refusal reaches the menu bar;
close it with a driver row ticked, so `cp_flush_close` writes `SYSTEM.CFG` from
a loaded image (§8.4); shed the image under a package load and re-open (§8.1);
and a Save As from a package with the file dialog's image already shed. Then the
5150, because every one of those is a disk operation and this tree's rule about
disks is that the number lands on the iron.

**And the thing to watch that no test will catch**: `tools/os88ovlchk.py` sees
near calls, not indirect ones, and this mechanism is built entirely out of
indirect far calls. The four existing tables of `.cold` pointers in `.text` are
already a review rule rather than a build gate; this adds one more, and it is
the same rule — *a table of module offsets may live in `.text` only if the
module alone dispatches through it.*
