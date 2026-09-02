# On-demand kernel modules

**Research document, not a contract — and now IMPLEMENTED.**
> The study below was written before the code and is kept as the reasoning
> behind it. What shipped is SPEC.md §2.8; where the two disagree, SPEC.md is
> the contract. §12 at the end is what happened when it was built.

SPEC.md is the binding contract for
what the kernel *is*; this was the study of a mechanism nobody had built.
Every figure was measured on this tree at `9ff3bd3` by the method in §6.1, and
the ones that are derived rather than measured say so.

The ask, in the requester's words:

> Investigate creating a new kernel concept: on demand drivers. This can
> likely reuse most of the existing driver architecture. One example of a
> usecase is the "Disk Format" command — rarely used, and could be loaded from
> disk, ran, and then discarded. The goal is to shrink the kernel and the
> kernel's memory usage. Find and recommend any other potential usecases in
> the kernel.

> **This document was rewritten once, and the first edition's mistake is the
> most useful thing in it.** It ranked candidates by how cleanly they could be
> *severed* — symbol counts, thunk counts, dangling references — and produced
> a recommendation (the Standard File dialog first, Cut/Copy/Paste second)
> that is wrong on the only axis that decides: **what the user is doing when
> the code is wanted, and how often.** §1 is that axis, stated as a test, and
> it disqualifies both. The seam analysis was not wasted — §6 keeps it, and it
> is what says the two surviving candidates are *feasible* — but it is the
> second question and it was asked first.

---

## 0. The verdict, up front

**Two candidates, on two different payoffs, and neither is the one the seams
pointed at.**

| | what it costs today | what on demand buys |
|---|---|---|
| **The Control Panel** (§31) | 3,204 B of `.cold`, resident on both builds forever | **~3 KB off both builds** — and its precondition is one the feature *already has* |
| **Disk Format** (§18.96/§22.12) | 1,357 B of `.cold` + 342 B of `.text` on `kern_big`, and it is **compiled out of `kern_small` entirely** | **`kern_small` gains a feature it does not have**, and `kern_big` gives back 2 KB |

Four findings:

1. **The test is about the user, not the code.** A feature may be loaded on
   demand only if **the system disk is already required to do it, or can be
   required without interrupting what the user was doing.** §1. On a
   one-floppy machine — the calibration machine — every load is a disk swap,
   and there are operations where that is simply not payable.

2. **MS-DOS drew this exact line and got it right.** `COMMAND.COM` is resident
   and implements `COPY`; `FORMAT` is an external utility on the system disk,
   never in RAM when it is not needed. Frequency and preparedness decide, and
   the answer has been known since 1981. §1.1.

3. **The mechanism is not the driver architecture — it is `.cold`.** Cold code
   is assembled at `vstart=0`, contains **zero data directives** (checked, not
   assumed), runs with `DS = KERNEL_SEG` and calls out only through the 107
   `cw_*` far shims. It is already position-independent at paragraph
   granularity and would run from a heap claim unchanged. Making the *cold*
   thunk load-and-dispatch spends **no `.text`** — the guard with 438 bytes
   left and no way to raise it. §5.

4. **Format must be a kernel module and cannot be a package.** Measured, its
   entire outbound surface is **four calls, three of them existing `cw_*`
   shims**, and two of those are `disk_read`/`disk_write` — kernel-internal
   primitives with **no API slot, deliberately**. An application cannot write
   raw sectors to an unmounted volume and should not be able to. §5.4.

---

## 1. The test

> **A feature may be loaded on demand only if the system disk is already
> required to do it, or can be required without interrupting what the user was
> doing.**

Everything else — seam width, `.bss`, thunk counts — is a feasibility check
that comes *after* this one and can only ever say no.

The reason it binds so hard is the machine this project is calibrated against:
**one 360 KB floppy drive** (docs/FIELD-MACHINES.md). There, "load a module
from the system disk" means *the user physically swaps a disk*, and the
question is never "is 200 ms acceptable" — it is "does this operation already
have a trip to the disk box in it".

Run against the candidates:

| feature | system disk already required? | verdict |
|---|---|---|
| **Control Panel** | **Yes** — it writes `SYSTEM.CFG` there when it closes (§51.5.1) | **passes** |
| **Disk Format** | No, but it can be — the operation is *prepared*, and the user is already handling disks | **passes** (§3) |
| Standard File dialog | **No, and the requirement is perverse** | fails |
| Cut/Copy/Paste | **No** | fails |

### 1.1 The precedent: `COMMAND.COM` implements `COPY`, `FORMAT` is external

This is the whole taxonomy, and it is worth stating because it is not a
judgement call — it is what every 8-bit and 16-bit system with removable media
converged on:

- **`COPY` is in the resident interpreter.** It is one of the two or three
  things a user does most, it acts on whatever disk is in the drive, and
  requiring the system disk to copy a file would make the system unusable.
- **`FORMAT` is a file on the system disk.** It is rare, it is deliberate, it
  is preceded by going and *getting* a blank disk, and it prompts —
  `Insert new diskette for drive A: and press ENTER` — which nobody ever
  considered a defect because the swap was already part of the job.

os8088 is a Macintosh-shaped system rather than a DOS-shaped one, but the
constraint that produced that split is the *hardware*, and the hardware is the
same. Where this tree has already made the same call it made it the same way:
the Task Manager is a package on the system disk (§28), the hard disk's
partition/format/install tool is loaded on demand by its own driver (§52.11),
and both are things you go and *do* rather than things you do while doing
something else.

### 1.2 Why the file dialog fails

The first edition made it the top recommendation on the strength of a
**7-thunk seam**, the narrowest in the kernel. The use case kills it outright:

> Note Pad, on a one-floppy machine, saving a file to the apps disk.

The apps disk is in the drive **because that is where the document is going**.
The Save dialog is *how the user reaches that disk* — and on demand, opening
it would require first swapping to the system disk, waiting for a load, and
swapping back. The chooser cannot be the thing that costs you the disk you are
choosing on.

It is also not rare. Every save, every open, in every application.

### 1.3 Why Cut/Copy/Paste fails

Worse, on both counts. The user has clicked a file **on the disk in the
drive** and wants to act on it; the operation is meaningless without that disk
present, so the load would have to happen, be swapped away from, and swapped
back to — around a selection made on a listing that the swap invalidates
(§22.8's whole subject). And it is one of the most common actions in the
system, not a rare one.

`COPY` lives in `COMMAND.COM` for exactly this reason.

---

## 2. The Control Panel

**3,204 bytes of `.cold` on both builds, and the precondition is already
paid.**

The argument is not "it is rarely used", which is true but weak. It is:

> **The Control Panel is a configuration applet that persists its settings to
> the system disk.** `cp_flush_close` writes `SYSTEM.CFG` when the panel is
> CLOSED (§31.8/§51.5.1) — one floppy write per session, to `KERNEL.SYS`'s own
> volume, which it locates and refuses without. **Any panel session that
> changes anything already requires the system disk.** On demand moves that
> requirement from close to open, and asks for nothing the feature did not
> already ask for.

What is genuinely new is that a session which changes *nothing* — opening the
panel to look — would newly need the disk. That is the honest cost and it is
small.

### 2.1 The seam is the cleanest in the kernel

15 symbols, of which **6 are already the `cpf_*` cold thunks**. The other 9
are four words of state (`cp_sel`, `cp_dirty`, `cp_wdirty`, `cp_nst`), three
data pointers (`cp_tpl`, `cp_sname`, `cp_s_dsdrv`) and two constants
(`CP_ITIME`, `CP_IDRV`) — every one of which stays resident in `.text`
anyway, because §5 moves code and never data. It has **zero `.bss`**, every
entry point is on the UI task with the gfx lock held, and nothing calls it in
a loop.

### 2.2 The `drv_notice` objection, and why it is weaker than the first
### edition claimed

docs/KERNEL-MEMORY.md says the panel is *"the window you want when a driver
will not attach, and where `drv_notice` sends you"*, and the first edition
made that the reason to take the panel **last** — two of the seven `DRVE_*`
codes being conditions under which the panel could not load either.

**Checked against `kmain`, that is mostly wrong**:

```
2175:    call drv_boot               ; ...and load what SYSTEM.CFG asks for
2188:    call wm_paint_all
2208:    call drv_notice             ; ...and only NOW say what did not load
2211:    jmp ui_task                 ; task 0 becomes the UI task; never returns
```

`drv_notice` runs **before `ui_task` starts**, so before a single event has
been dispatched and before the user has had any opportunity to touch the
drive. **`DRVE_DISK` is unreachable there** — the machine booted off that disk
moments earlier and nothing has asked for it to be removed. What survives is
`DRVE_MEM`, and it survives weakly: a driver may claim up to `DRV_MAX_KB` = 40
KB where the panel wants ~4, and `drv_load`'s failure path calls
`drv_release` before it returns, so the memory that could not fund the driver
is free again and will usually fund the panel.

The residual is a machine so short of heap that neither fits. That deserves
a toast rather than a restructuring — `drv_errstr` is already the bounded
table of strings, and §59.6 already put the panel's *own* verdict in the menu
bar for the same species of reason. **Worth building, not worth blocking on.**

### 2.3 What it does not disturb

`cp_flush_close` runs *from* the module, so the image must still be resident
when the panel closes — it will be, since the close is a call into it, but it
makes the pin in §7.1 an open-to-close span for this module rather than a
per-call one. That is a per-module property and §7.1 says so.

---

## 3. Disk Format — and the first edition's misreading

**The first edition ruled this out. It was wrong, and the error was one of
reading rather than of measurement.**

It took SPEC.md §22.12's opening — *"A floppy that reads and is not FAT12
shows `No os8088 disk (A:)` and nothing else… **File ▸ Format Disk…** is the
way out"* — as a statement of what the feature is FOR, and concluded that the
disk in the drive at the moment of use is always a foreign one that the module
therefore cannot be read from.

That paragraph describes **one route in**, not the purpose. The purpose is:

> **complete an action to make a disk empty and usable.**

Overlapping, and different. It is not disk *recovery* — it cannot recover
anything, it erases. It is **preparation**: the user has a blank or unwanted
disk and wants a working one. Which means the user is already standing at the
machine handling disks, and a swap is not an interruption of the task — it *is*
the task.

### 3.1 Frequency settles it

Formatting a disk is among the **rarest** deliberate actions in the system.
Copying and pasting is among the most common. The first edition compared them
on seam width, where they look similar; on frequency they are not remotely
alike, and frequency is what §1 measures.

### 3.2 The `kern_small` argument runs the other way

The first edition observed that `dskw_fmt_*` and its UI sit inside `%ifndef
KERN_SMALL`, and concluded that the machine which most needs memory already
does not pay — so on demand would only save `kern_big` 2 KB.

**That reads a missing feature as a saving.** What that guard actually means
is:

> **A 128 KB machine cannot format a floppy at all.**

It has no Format item on its File menu. On demand does not save that machine
2 KB — it **gives it the feature**, at a resident cost of the menu item, a
predicate and a stub. That is a much better payoff than the one the first
edition costed, and it is the argument for doing it.

### 3.3 The swap prompt is not new UI — it is `FS_EDIT` mode 6

The one real design consequence. On a one-floppy machine the order has to be:

```
    system disk in A:  ->  File > Format Disk...  ->  module loads
                       ->  "Insert the disk to format, then Enter"
                       ->  probe, size, confirm  ->  format
```

`fm_c_format` currently calls `dskw_fmt_probe` **on the spot**, which after an
on-demand load would probe the system disk — the wrong disk. So a step has to
go in front of it.

That step costs almost nothing, because the machinery exists: `FS_EDIT` is the
Disk window's status-line mode byte, **modes 1–5 are used and 6 is free**, and
mode 5 is already a *two-line* question (the row above the status line carries
`Format A: as 720K?` and the status line carries the answers — §22.12). A mode
6 reading `Insert the disk to format  Enter=ready  Esc=no` is the same
mechanism with different strings, and `FS_FERR`/`FS_LDST` left two free bytes
at +14/+15 in the state block if per-window state is wanted.

And it is **conditional**: the prompt is owed only when the volume being
formatted is the volume the module came from. A two-drive machine, a machine
with a hard disk (where the system volume is the boot partition — §52.10.3),
or a second format in the same session with the image still cached (§7.2) all
skip it entirely.

### 3.4 It has to go in second

`kern_small` stands at **exactly zero bytes of spare** against its guard (§4),
so restoring Format there costs a 512-byte rung it does not have: the strings
stay resident in `.text` (§5 moves code, not data — 183 bytes here) plus a
stub. **The Control Panel's 3,072 bytes fund it**, which is the sequencing
argument and the reason §8 orders them that way.

---

## 4. Where the kernel stands

```
kernsize[big]: sections   text 59,394  bss 5,704  cold 23,200  lowbss 7,762  ovl 3,138
kernsize[big]: footprint  KERN_SIZE 102,912 of KERN_BUDGET 104,960 -> 2,048 spare (4 steps)
kernsize[big]: segment    .text+.bss 65,098 of KERN_CODE_MAX 65,536 -> 438 left
kernsize[big]: ladder     HEAP 0x1980 = 102.0 KB

kern_small:               KERN_SIZE  97,280 of KERN_BUDGET  97,280 -> 0 spare
```

**Zero.** The next byte added to `kern_small` anywhere fails to build.
docs/KERNEL-MEMORY.md's move 21 was the last 1 KB and its terms were move 5's:
headroom for ordinary growth, not an invitation. There is none left.

On the 128 KB machine the heap is what is above the kernel: **26.0 KB** under
`kern_big`, **31.5 KB** under `kern_small`.

### 4.1 The four levers, and what each relieves

| lever | `KERN_CODE_MAX` (segment) | `KERN_BUDGET` (footprint) |
|---|---|---|
| **`.ovl` boot overlay** (§2.5) | relieved | **relieved** — lands in the FAT window, overwritten by the first mount |
| **`.cold` cold segment** (§2.6) | relieved | **not relieved** — still resident |
| **out to a package** (§28's Task Manager) | relieved | relieved |
| **on-demand module** *(this document)* | not spent | **relieved** |

The second row is the point. **Moving a module cold to fix a footprint overrun
is a no-op that looks like a fix** — docs/KERNEL-MEMORY.md says so already —
and 23,200 bytes of this kernel took that route and are still resident.

---

## 5. The mechanism: `.cold` is it, minus where it lives

`section .cold start=COLD_START vstart=0`. Three properties, all checked:

- **It is pure code.** Every `.cold` block in `kernel/` scanned for `db`, `dw`,
  `dd`, `resb`, `resw`, `times` and `incbin`: **0 directives**. Cold modules
  put their data back in `.text` on purpose — `diskw.inc` labels the switch
  *"DATA, so back to the kernel segment"*.
- **It runs with `DS = KERNEL_SEG`.** Every variable, string and table is
  reached at the same offset `.text` reaches it at. **A module that moves does
  not re-express one data access.** This is the entire difference from a
  package.
- **It never near-calls out of itself.** 107 `cw_*` shims are its whole
  outbound surface, each an absolute `call KERNEL_SEG:cw_x`, and
  `tools/os88ovlchk.py` fails the build if a near call crosses.

So cold code is **already position-independent at paragraph granularity**. The
only thing tying it to `COLD_SEG` is the constant in its inbound thunks, and
that is one word.

### 5.1 What changes, and what does not

The `.text` thunk **does not change**: `cp_paint` stays
`call COLD_SEG:cpf_cp_paint`. The *cold* thunk `cpf_cp_paint` becomes the
load-and-dispatch stub, and cold code is always resident. **The resident cost
against `KERN_CODE_MAX` is zero** — which matters, because that guard has 438
bytes left and cannot be raised by any conversation.

### 5.2 The image and its header

The module's `.cold` output, at `vstart=0`, shipped as `<NAME>.DRV` in the
system volume's root — which buys hidden+system+read-only *by extension* from
`os88disk.py`'s `sys_attr` (§19.6), the installer's `*.DRV` copy rule, and
`ld_check_hdr`'s refusal, with no tool-chain change. Header version **5**, so
both `ld_check_hdr` (3) and `drv_check` (4) refuse it too.

A kernel module needs no `call bp / retf` dispatcher — the kernel knows the
entry offsets, because it built them. That is cheaper and also a hazard: a
stale module beside a newer kernel would far-call the wrong offset, silently.
So the image carries an entry table and a **build stamp**:

```
  0   dw  0x384F          ; 'O','8'
  2   db  5               ; kernel module (package 3, driver 4)
  3   db  module id
  4   dw  the kernel's BUILD NUMBER      <- tools/buildnum.py, already here
  6   dw  image size = the file's whole size
  8   dw  entry count
 10   dw  entry[0] .. entry[n-1]         ; offsets in this image
```

`buildnum.py` already moves on every commit (§14.2), so a module and the
kernel that can call it agree by construction.

### 5.3 The loader

`drv_load` minus everything that is about being a driver — no row, no class,
no `drv_publish`, no `DRVV_ATTACH`, no tier. What survives is ~130 bytes and
belongs in `.cold` beside the stubs that call it:

```
    drv_vol_bank                 ; the user may be anywhere (§51.5.2)
    drv_mounted                  ; is the system volume reachable?
    dskw_stat                    ; how big is it
    mem_claim_hi                 ; one claim, at the size the directory reported
    dskw_read                    ; the whole file, by name
    <header check>
    drv_vol_back                 ; preserves CF
```

### 5.4 Why Format cannot be a package

Measured — the formatter's *entire* outbound surface:

| calls out to | what it is |
|---|---|
| `cw_disk_read` | kernel-internal sector read |
| `cw_disk_write` | kernel-internal sector write |
| `cw_dsk_vol_row` | the volume table row |
| `dskw_ioerr` | `diskw.inc`'s own error mapper — needs a shim or moves with it |

Everything else it calls is its own. **Three of the four are `cw_*` shims that
already exist**, which is as clean a fit as this mechanism could ask for.

And it is decisive about the *shape*: `disk_read` and `disk_write` have **no
API slot, deliberately** — nothing published lets anything write raw sectors
to an unmounted volume, and an application should not be able to. So Format is
a **kernel module** (`CS` = the claim, `DS = KERNEL_SEG`, out through `cw_*`)
and not an application package, and that is a fact about the operation rather
than a preference. `HDDTOOL.DRV` reached the opposite conclusion for the
opposite reason (§52.11): it needed its *driver's* segment, not the kernel's.

---

## 6. The measurements

### 6.1 Method

A scratch copy of `kernel/` with one `%include` removed or one `%ifndef`
guard forced, assembled with the real flags (`-f bin -w+error -DKERNSIZE`) and
the real include paths. **nasm is the oracle** — anything still needed shows
up as an undefined symbol, and the loop stubs each one and re-assembles until
it converges. This is docs/HDD-SPLIT-PLAN.md §4.1's severance, automated.
Sizes come from `kernel.asm`'s own `ks:` line, which is where `kernsize.py`
gets them, so there is no second opinion about how `KIMG_PARA` rounds.

### 6.2 The two candidates

Δ against `kern_big` at 102,912. The Format rows are **not** a severance —
they are the exact delta of the existing `%ifndef KERN_SMALL` guards, so they
are facts about the shipped build.

| | Δ`.text` | Δ`.cold` | Δ`.bss` | Δksize | seam |
|---|---:|---:|---:|---:|---:|
| **Control Panel** (`ctrl.inc`) | −672 | **−3,204** | 0 | −4,096 | 15 syms, 6 already thunks |
| **Format, engine** (`diskw.inc`) | −159 | **−751** | 0 | −1,536 | 4 outbound calls |
| **Format, UI** (`files.inc`) | −183 | **−605** | −1 | −1,024 | — |
| **Format, both** | −342 | −1,356 | −1 | −2,048 | — |

**Read the Δ`.cold` column for what actually moves.** A severance removes the
module's data too and a real conversion does not: only `.cold` leaves, and the
`.text` bytes are tables and strings that stay resident and reachable through
`DS`.

So, in rungs:

| | `.cold` today | after | footprint saved |
|---|---:|---:|---:|
| Control Panel out | 23,200 | 19,996 | **3,072** |
| …then Format's engine and UI out | 19,996 | 18,640 | **1,024** more |

less the dispatcher and per-entry stubs that go back into `.cold` (~150 bytes
each), which is **~3.9 KB on `kern_big`** — derived from measured section
sizes, not itself measured, and it will not be until something is built.

For `kern_small` the shape is different and better: the Control Panel's 3,072
arrives as **spare against a guard that currently has none**, and Format
arrives as a **feature that build does not have**, paying only its strings and
stub out of that.

### 6.3 The peak is better, unlike the HDD split

docs/HDD-SPLIT-PLAN.md §10 had to report the peak 3 KB *worse*, because the
tool image duplicated helpers it could no longer near-call. Nothing is
duplicated here — the bytes move, they are not copied — so the only overhead
is `mem_claim` rounding to whole KB. With the panel open: 4 KB claimed against
3,072 + rounding given back, which is a wash at the peak and a clear win at
rest. The panel and the formatter are never open at once.

### 6.4 What it costs in time

Derived from PERFORMANCE.md's 24 ms per 512 bytes delivered plus its
isolated-access note — *a first-sector read is a **seek**, ~80 ms average and
worse at the stroke's end* — a 3.2 KB module is **roughly a quarter to half a
second**. That is an inference from two measured figures and belongs in a
field run before anybody quotes it. §18.95's sector cache and §18.8.1's
per-volume FAT window both help on a second load, and §7.2 means there usually
is no second load.

For the Control Panel that lands on a window opening. For Format it lands in
front of an operation that writes every sector of a floppy, and is invisible.

---

## 7. The traps

### 7.1 A module must not be purgeable while it is entered

§7.2 makes the image a purgeable claim, and `mem_claim`'s shed-and-retry fires
on a refusal and takes the lowest-priority cache. If the Control Panel's own
image is a cache while `cp_onclick` is running inside it — and `cp_onclick`
reaches `drv_load`, which reaches `mem_claim_hi` — the machine can shed the
code it is standing on. It would not fault; it would run whatever the next
claim wrote there.

A one-byte nesting count per module, pinned while non-zero, incremented by the
stub before the far call. Per module and not one flag, because a module can
re-enter itself through a callback. **For the Control Panel the pin is an
open-to-close span**, not a per-call one, because `cp_flush_close` writes
`SYSTEM.CFG` from inside the image (§2.3).

### 7.2 Discard: shed, do not free

The obvious design frees the image when the feature closes. **Do not** — §50.6
already has the better answer. Tag it `MEM_PG_LOW` (*"losing it costs a little
I/O, or a visible pause"*, which is exactly a re-read): a second visit costs
nothing on a machine with room, the image is the first thing shed on a machine
without, a package load that would have been refused takes it instead of
failing, and §50.6.1 places purgeable claims inside the data arena below the
lowest region base, so a cached image cannot fragment the run a package needs.

For Format this is also what removes §3.3's prompt on a second format in the
same session.

### 7.3 The register the verb travels in

docs/HDD-SPLIT-PLAN.md §10 sprang this twice in one build: the verb was passed
in `BP`, which is the dispatcher's *address*, and the stub restored `BP` after
kernel calls that spend it. Both silent — nothing faulted, nothing hung,
nothing appeared on screen. Here there is no dispatcher (§5.2) so the entry is
an index into the header's table, but the stub is the one place in this
mechanism where a register contract is invented rather than inherited.

### 7.4 The load happens under the gfx lock

Every entry point of both candidates is called from the UI task with the lock
held. That is already what `drv_load` from a Drivers-page click does, and what
`ui_tm_open` does for `TASKMGR.O88`. Not new.

### 7.5 A refusal is a normal path, and both callers need one

No system disk, a full heap, a corrupt image, a build-stamp mismatch. §50's
rule says refusal is normal and every claim in the tree has a fallback; the
work is that **two features that could not previously fail now can.** The
Control Panel's answer is a toast (§2.2); Format's is the same, and it is
strictly better than the alternative — a Format that refuses because the
system disk is out is a sentence, where a Format that half-ran is a disk.

### 7.6 The stale-mount hazard around Format's swap

Between the load and the format the user changes the disk in the drive. §18.9
and §18.9.3's BPB banking exist precisely to *avoid* re-reading a BPB, and
§18.9.3 says a floppy skips revalidation only when a **caller asserts** it
inside a batch bracket. Mode 6's Enter is the assertion running the other way:
it must **invalidate**, and `dskw_fmt_probe` must re-probe the drive it is
about to write. Getting this wrong formats to the geometry of the disk that
has been taken out.

### 7.7 A window that outlives its image is a paint into freed memory

docs/HDD-SPLIT-PLAN.md §6.1 exactly. The Control Panel's window dispatches
through `W_DISP`/`W_SEG`; shed the image while the window still names it and
the next paint runs freed memory. §7.1's pin covers it. **This is the trap to
test first and it fails as a hang, not as an error.**

### 7.8 The build has to keep them in step

The module images build from the same tree as the kernel, carry its build
number, and land on **both** shipped floppies and in the installer's copy set.
A kernel that ships without its modules is a machine whose Control Panel does
not open — so `os88disk.py` should refuse an image whose kernel names a module
the volume does not carry, the way it already refuses an over-long root
listing.

---

## 8. What this does not change

- **No API slot, no `.o88` invalidated, no package rebuilt.** §20.8 rule 4 is
  not engaged.
- **`KERN_CODE_MAX` is not spent** (§5.1).
- **`.cold`'s contract is unchanged** — same `vstart`, same `DS`, same `cw_*`
  shims, same `os88ovlchk.py`.
- **No module's data moves**, so no string staging (§31.9's `DSV_CPNAME` trap)
  and no `cs:` overrides anywhere.
- **The `kern_small`/`kern_big` split is untouched.** This is for what the
  split cannot reach — and for Format it *undoes* a split that was a feature
  removal.

---

## 9. Decisions for the owner

1. **Control Panel first, Format second?** §3.4's arithmetic says it has to be
   that way if Format is to reach `kern_small`, which is its whole payoff:
   the small build has 0 spare and the panel is what funds the strings and the
   stub.
2. **Does Format's `FS_EDIT` mode 6 prompt read acceptably?** §3.3. It is
   DOS's prompt and this tree's own two-line confirmation mechanism, but it is
   the one user-visible change in the whole document.
3. **Is a look-but-change-nothing Control Panel session worth a disk swap?**
   §2. Every session that changes something already pays it at close.
4. **`.DRV`, or a new extension?** `.DRV` buys the attributes, the copy rule
   and the loader's refusal for free, at the cost of a file that is not a
   driver being called one — docs/HDD-SPLIT-PLAN.md's decision 4, unresolved
   then and unresolved now.
5. **Is the `DRVE_MEM` toast (§2.2) a prerequisite or a follow-up?** It is
   worth having whether or not the panel ever moves.

---

## 10. If it goes ahead

1. **The header, the loader and the pin** — `mod_need`, `mod_pin`,
   `mod_release`, all in `.cold`, plus `tools/os88mod.py` to stamp an image.
   Nothing calls any of it. Check that `.text` grows by **zero**.
2. **The `DRVE_*` toast** (§2.2), so `drv_notice` can report without a window.
   Worth having on its own merits and a prerequisite for step 5.
3. **Build `CTRL.DRV` as a second output of the same source** and ship it on
   both floppies, while `ctrl.inc` is *still* compiled in. Two copies of the
   same bytes, one unreachable — the commit that proves the build.
4. **Route `cpf_*` through `mod_need`**, resident copy still present as the
   fallback.
5. **Delete `ctrl.inc`'s `.cold` from the kernel image.** The first commit that
   can break something, and the one to A/B: the same scripted session against
   both builds, framebuffer byte-identical — `make REDRAWFULL=1`'s discipline
   applied to a different question.
6. **`FORMAT.DRV`**, the same steps compressed now the mechanism exists, plus
   `FS_EDIT` mode 6 and §7.6's invalidation.
7. **Turn Format on in `kern_small`** — the commit that is the actual point of
   step 6, and the one to measure against the guard.

**How it gets tested.** `make marty` throughout: this is 8088 code, all three
adapters, and MartyPC's floppy turns (PERFORMANCE.md Sets 35/37) so the load
cost is within a measurement quantum of the iron rather than 30x fast. Drive:
the panel opened and closed twice in one session (no leak, second open free);
opened with the heap deliberately filled (the refusal must reach the menu
bar); closed with a driver row ticked, so `cp_flush_close` writes from a
loaded image (§2.3); the image shed under a package load and re-opened (§7.1);
a Format with the system disk out; a Format with the disk swapped between load
and Enter (§7.6); and two Formats in one session, the second of which must not
prompt. Then the 5150, because every one of those is a disk operation and this
tree's rule about disks is that the number lands on the iron.

**And the thing no test will catch**: `tools/os88ovlchk.py` sees near calls,
not indirect ones, and this mechanism is built entirely out of indirect far
calls. The four existing tables of `.cold` pointers in `.text` are already a
review rule rather than a build gate; this adds one more, and it is the same
rule — *a table of module offsets may live in `.text` only if the module alone
dispatches through it.*

---

## 11. Everything else, and why not

Kept from the first edition, because the survey is still worth having — but
re-sorted so that §1's test comes first and the seam only ever confirms.

**Disqualified by §1 (the user, not the code):** the Standard File dialog
(§1.2), Cut/Copy/Paste (§1.3), `assoc.inc` (every document open), `icons.inc`
(every desktop paint), `loader.inc` (it is what loads things), `clip.inc`
(per copy, and 512 bytes), `fsx.inc` (`fsx_wait` is a game's **frame clock** —
§53.2's whole argument is that the bracket has no jitter in it), `files.inc`'s
6,590 bytes of `.cold` (the Disk window is Locator, and Locator is what the
machine is when nothing else is running), and the Disk window's own New
Folder, Rename and Delete — all common operations acting on the disk in the
drive, which is `COPY` in `COMMAND.COM` again.

**Already done, and the precedents:** the Task Manager is a package on the
system disk (§28), and the hard disk's partition/format/install tool is loaded
on demand by its own driver (§52.11). Both pass §1 for the same reason Format
does.

**Not a candidate but worth knowing:** `xmem.inc` is genuinely rare and
already removed from `kern_small` (§41.11) — but §3.2's correction applies to
it too, and it is the one place the first edition's reasoning survives
unchanged, because nothing about XMS is a *user* action to be restored.
`splash.inc`'s 961 bytes look like a candidate and are not: after boot the
only live entry is `spl_step`, a compare and a `ret` called once per sector
from `dsk_xfer`, and the rest must be resident within the image's opening
`SPL_RESIDENT` sectors because the boot sector ticks the bar while the kernel
is still arriving. If those bytes are wanted, the lever is `.ovl`. The RTC
write paths (`clk_at_write` 141, `clk_rp_write` 130, `clk_ns_write` 99 — 370
bytes) are reachable only from the Control Panel's Date/Time page and are not
a candidate of their own: they are three routines that should **move into the
Control Panel's image**, and they are the first thing to look at once §2 has
landed.

**And a shape worth recognising.** The largest single routine in the kernel is
`osapi_table` at 944 bytes, and it is a table; the next is a 258-byte icon.
There is **no hot spot anywhere** — 82,594 bytes of code with nothing over 200
bytes in it, which is §5.7's finding about `gfx_pixel` in another register. The
unit of on-demand loading is therefore a **feature**, never a routine, and any
proposal naming a function rather than a user-visible operation is proposing
to spend a disk read to save a hundred bytes.


---

## 12. What happened when it was built

Shipped as SPEC.md §2.8. The shape survived intact — `.cold` was the
mechanism, no `.text` thunk changed, and the seam fell where §6 said it
would. Five things are worth recording.

**The numbers came in close, and the second candidate changed sign.**

| | predicted | built |
|---|---:|---:|
| Control Panel out | −3,072 | **−2,560** |
| Format out | (not recommended) | **−1,024**, then +512 for the swap prompt |
| `kern_big` total | — | **102,912 → 99,840** |
| `kern_small` total | — | **97,280 → 96,256, and 0 → 1,024 spare** |

**Those four are the DELTAS this work is responsible for, measured on the
branch, and the totals are not the tree's current position** — three rounds of
integration landed between them and `elendilon` (the store above 1MB became an
overlay, the cursor grew a per-window picture, a hidden Control Panel row
stopped having to be last, and a moved window replays its content). Read the
left-hand column as *what the modules cost or saved*; docs/KERNEL-MEMORY.md's
blessed baseline is where both builds actually stand, and it also carries what
`kern_small` needs to build at all again, which is no longer 0.

The panel came in 512 short of the estimate because the loader, the stubs and
the refusal string go back into `.cold` and `.text`. **The estimate that
mattered more was `KERN_CODE_MAX`'s, and §0's finding 2 was too strong**: the
mechanism spends no `.text` on *dispatch*, but the five `cw_*` shims the
loader calls out through, two thunks, the module table and two refusal strings
are `.text` all the same — slack fell **438 → 300**. The claim to make is that
the *dispatch* is free, not that the feature is.

**`kern_small` is where the story ended up, and it is Format's, not the
panel's.** §3.2's correction was right and understated: the small build now
*has* a floppy formatter, which it never had, **and** the modules bought back
1,024 bytes against a guard that had none. Both from the same round — and the
second half has since been spent by other work rather than by this: at the
integration branch `make small` is 512 bytes over, where without the modules it
would be 1,536. The feature survives; the spare did not, which is the ordinary
fate of spare and the reason the guard is a guard.

**A second stamp was needed and the plan did not see it.** §5.2's build number
answers "another commit" and cannot answer "another BUILD of this commit" —
`make small`, `VIDEO=cga` and `FONT=` are all one commit with the kernel's
data at different addresses. `MOD_STAMP` (the section sizes summed) is that
second word, and every image rule that builds a kernel outside `build/` now
builds its own modules beside it.

**Five bugs, and all five were silent.** §7.3 predicted the register trap and
missed the sharper one next to it: **an entry far-called must end in `retf`,
and every `.cold` body ends in the near `ret` it has always had**. Pointing
the header's table straight at the bodies left CS as the module and marched
execution forward through the image — which reached the panel's own drawing
code and painted a Control Panel, with no window, over the desktop, once a
minute. The fix is a 4-byte far stub per entry inside the image, which is
`.cold`'s own `cpf_*` idiom moved one segment along. The other three:
`cpf_cp_tick` loaded the image every minute to run a no-op (it is gated on the
resident `cp_tick_due_x` now); `cp_open` had no `ret` and fell into
`cp_paint`; and eleven near calls from `.modc` into `os88ui_glyph` /
`os88ui_btn` crossed a boundary **`tools/os88ovlchk.py` could not see, because
it globbed `kernel/*.inc` and that file is under `apps/`**. It scans it now,
and that is the more important half of the fix — a file that emits code into
the kernel belongs in its list whatever directory it is in.

**The fifth is the one to read, because it was found by a MERGE and not by a
test.** `mod_fp`'s stride is spelled twice — `mod_fpi` *shifts* by it to arm a
module's slots, and each module's own base is an `equ` that *multiplies* by it
to dispatch — and the two disagreed: the shift said 5 (32 bytes) where four far
pointers are **16**. Module 0 is at offset 0 whatever either says, so **the
Control Panel worked perfectly**; the formatter's slots were armed sixteen
bytes past the end of the array while its thunks far-called a block nothing had
written. Two things about how it surfaced are worth keeping. It did not fault:
`mod_fp` is `.bss`, so the overwrite landed on whatever the section's *ordering*
put after it — which is a decision taken in files nowhere near this one, and
which the merge changed. And what it happened to land on afterwards was
`[mod_fsz]`, the image size, which the arm loop's own bound check then read one
iteration later — so the module refused itself with a perfectly good file on the
disk, and the swap prompt (§2.8.5) came up on a two-drive machine with the
system disk in A:. The fix is one shared `MODFP_STRIDE` with a
`%if (1 << MODFP_SHIFT) != MODFP_STRIDE` beside it, which is the tree's own
answer to a constant written twice (`CUR_GW`/`CUR_GH` against the cursor
bitmap). **Two spellings of one number is a bug whatever they currently say.**

**§5's traps were mostly not the ones that bit.** §7.1's purgeable-claim
hazard never arose, because §7.2 was taken as written and the images are
ordinary claims freed by the feature. §7.4's under-the-lock load is real and
uneventful. What did bite is not in §7 at all: the shared-helper crossing
above, and the fact that a *test harness* clicking 30 pixels to the left of a
divider lands in the item list rather than the page — which cost two runs and
is why the settle timeouts in this work were test bugs twice over. A third
harness defect came out of the stride hunt and is fixed in the tool:
`os88mouse.py`'s `menu`, `drag` and `click` sent their button edges as bare
packets where `dblclick` has always **proven** them against the published
`mouse_btn`, so a release dropped by the 1200-baud UART left the pull-down open
with the right item highlighted and the command never run — a screenshot of
which is indistinguishable from a menu being used correctly.

**What is not verified.** `os8088_xt_vga` does not boot to a desktop in this
container on this kernel *or* on the one before it, so VGA is untested and is
not a regression; CGA, Hercules and `kern_small` are each driven end to end.
And the swap prompt's *first* step — the one a one-floppy machine hits — is
built and reasoned about but has only been exercised on the path where
`mod_need` succeeds; the machine that needs it is the field machine.

## 13. The third module: the disk cloner (SPEC.md 18.99)

Written after the fact, like §12, because the mechanism this plan built turned
out to have one more obvious tenant.

**It passes §1 the way Format does, with one more term.** Preparing a disk has
a trip to the disk box in it; *cloning* one has two, because a one-drive
machine has to be handed the source and then the target. So the load is not an
interruption of the task — the task is already a sequence of disk swaps, and
`CLONE.DRV` costs one more.

**It is the first tenant that pays the resident cost this plan predicted, and
the first to route around it.** §5 records that a module's *data* stays
resident: cold code carries none and reads through `DS = KERNEL_SEG`, so
`dskw_fmt_tab` is in `.text` even though every instruction that reads it left.
The cloner has about thirty words of state and a 512-byte keep of the source's
boot sector, and it pays for none of them — they live in the head of the heap
claim it takes for the buffer (SPEC.md 18.99.1), so `[clo_seg]` is the whole
resident footprint of the operation and a machine that has never cloned a disk
carries two bytes. **That is the shape to copy**: a module whose feature has a
natural allocation should keep its state inside it.

The status line's own composer went into the image for the same reason. It is
*code* — the strings it stages are data and stay behind — and a clone prompt
can only ever be on screen while the image is loaded, which is what makes the
far call safe rather than clever.

**One entry, not four.** §5's `mod_fp` is a far pointer per entry and a thunk
per entry, at about twenty bytes of `.cold` each. The cloner has four verbs
and publishes **one** entry: the verb rides in `DL` and the image dispatches
on it internally. Nothing about §5 changes; the four-verb surface simply costs
one slot and one thunk instead of four of each. `MOD_NENT` is still 8.

**§3.3's swap prompt was not general, and now is.** Mode 6 asked for the
system disk and then, at step 1, for *the disk to format*. The clone needs
exactly that sequence and a different second line, so mode 6 gained a
`[fm_swapwho]` byte — one byte, one branch, and no ladder in `files.inc`
learned a new mode. **Both steps are wanted**: a cloner that probed
immediately after step 0 would take the *system* disk's geometry for the
source's, which is §3.3's own hazard arriving a second time in a feature
written by somebody who had read §3.3.

There is a third instance of the same hazard that §3.3 does not cover, and it
cost a debugging session on the format path too: `fmv_sync` compares
(drive, cwd), finds neither has moved, and returns **without touching the
drive** — so after a swap it hands the caller the previous disk's BPB.
`fm_clone_go` mounts outright.

**MOD_MAX is 3.** `mod_fp` grew by one `MODFP_STRIDE` (32 bytes of `.bss`),
which is the per-module cost §5 costed and the only one that scaled.

## 14. …and the formatter took the same treatment

§13's `[cs:si]` is not the cloner's trick; it is the mechanism's, and it was
applied to `FORMAT.DRV` in the same change. `dskw_fmt_line_x` is a fifth entry
that letters the confirmation's two lines, mode 6's step-1 line and both
verdicts out of strings that are now in the image — so `files.inc`'s
`.fs_st_fmt` went from ninety bytes of composition to two calls, and 'Format
A: as 720K?', `Spc=size  Enter=yes  Esc=no`, `Insert the disk to format`,
`Formatted B:` and `Made 360K, not 720K` all left the kernel.

**What §5 costed as unavoidable was the strings, and it is not.** This plan's
own §5 said a module keeps cold code's properties "minus where it lives", and
took "DS still KERNEL_SEG" to mean the data has to stay — which was true of
every module until one was written whose feature is *mostly text*. Between
them the two modules gave back **315 bytes of `.text` and 84 of `.cold`**,
which is more than the whole clone feature cost the kernel in the first place.

Three shapes could not go, and they are the same three in both modules: the
menu item, the step-0 swap prompt that exists *because* the image is not
there, and a message said after the drop. The third has a way out — compose it
while the image is loaded and let `toast_show` copy the buffer — and both
modules take it. The first two do not, and that is the residue.

`dskw_fmt_tab` stayed behind on purpose. §2.8.6 has the argument: a string has
one reader and a table has several, and each of the formatter's `[si+DFMT_*]`
dereferences is a place to get the segment wrong inside the code that erases
disks.
