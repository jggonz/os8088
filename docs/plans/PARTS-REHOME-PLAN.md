# THE LOADER FREES ITSELF — a package's loader as a disposable segment

> **Status: RESEARCH, nothing built.** The cost below is estimated against
> measured comparables in this tree and every claim is sourced to a line of
> kernel. It is ~150 resident bytes, and §6 is where that number comes from.
>
> **Read 4.1 and 5.2 first if you read nothing else.** Two things are not
> obvious and both would have been found late.
>
> **4.1** — after the switch the running program is *inside* a claim rather
> than at the base of one, and `mem_own`, the fence on every memory slot, says
> no to it. It would pass on a floppy and fail on a hard disk, at the program's
> first `OSAPI_MEM_CLAIM`, long after a successful launch. The fix is eleven
> bytes and a routine that already exists.
>
> **5.2** — the loader's job does NOT end when it has loaded. `op_seg` is an
> *accessor* whose table is in the loader's image and whose state is in the
> loader's bss, so freeing the loader destroys "where is part N" for a program
> that still needs it. The handoff has to carry the segments, and the place it
> carries them in decided the shape of the kernel arm.

---

## 1. The ask

A package that uses parts pays for its own loader, for ever:

* the LOADER's segment claim — its image and bss — is held for the life of the
  instance, and after `op_load` returns there is nothing in it anybody needs;
* the parts standard's own code sits inside the running program's 61,440
  bytes, which is exactly the budget the program ran out of and went to parts
  to escape.

The proposal: the loader loads the parts, tells the kernel *"the program is at
segment Y"*, returns, and is freed. The program then runs from Y **as though
it had never used parts at all** — an ordinary single-segment package, no
`OS88_PARTS_*`, no `op_seg`, no gates, and the whole 61,440 to itself.

Lazy parts are excluded and that is the ask's own scope: `op_fetch` and
`op_drop` are loader code, and a loader that has been freed cannot fetch.

---

## 2. What the launch path does today, and where the seam is

`ld_start` (kernel/loader.inc:949, `.cold`) is nine numbered steps. Three
matter here:

| step | what it does |
|---|---|
| 7 | zeroes the bss: `mov di,[ld_img] / mov cx,[ld_bss] / rep stosb`, ES = the region |
| 8 | calls the entry proc — `mov ds,[ld_base]` then `call far [es:ld_fp]`, where `ld_fp` is `{PKG_DISP, ld_base}` |
| 9 | registers the instance: `I_SPTR = [ld_base]`, `I_SIZE = [ld_need]`, the name from `[ld_base]:16`, the icon flag from `[ld_base]:LD_H_FLAGS`, then binds the window the entry returned in BX and publishes `I_STATE` |

**The seam is between 8 and 9**, and it is a good one: *nothing about the
instance is committed until step 9*. `I_SPTR`, `I_SIZE`, the name, the icon
and `I_STATE` are all written there, off `[ld_base]`. Change `[ld_base]`
between the two and every one of them describes the new segment with no
further edit.

Better still: **step 8's far call is the loop-back target.** So the whole
re-home is *"re-point `ld_base`/`ld_img`/`ld_bss`/`ld_ent` at Y and jump back
to the call"* — the second entry is made by the same three instructions that
made the first, with the same contract.

**Step 7 is deliberately NOT re-entered.** It is the `rep stosb` that zeroes
the bss, and on this path the program's bss arrived zeroed inside its own part
with the loader's handoff vector written into the head of it (§5.2). Zeroing
would erase the one thing the program is waiting to read.

### 2.1 Why the kernel has to be the one to call it

The loader cannot far-call Y's entry itself and then be freed: it would be on
the call stack. Control must leave the loader's segment entirely before the claim
goes, and the only thing that can do that is the caller — `ld_start`. That is
what forces the "tell the kernel and return" shape rather than a purely
package-side one.

### 2.2 `OSAPI_PKG_START` is already out of scope

The second launch door refuses parted images outright —
`test byte [es:si+LD_H_FLAGS], 4 / jnz .bad`, *"parts, and no file to read
them out of"* (loader.inc:1186). Nothing to do.

---

## 3. Ownership — the part that decides whether this is cheap

A claim's `MC_OWN` (kernel/memory.inc:73) is **one of two kinds**:

* `0..INST_MAX-1` — an instance **slot**. `ld_alloc` stamps the package's
  region this way (`call ld_slot` at loader.inc:900).
* a **segment** — what `mem_own` returns for a package's own claims:
  `.yes: mov bx, es` (memory.inc:3709), and `osapi_mem_claim_dma_x` passes
  that straight through as the owner word.

Teardown reads both. `mem_free_rec_x` (memory.inc:3205) sweeps the slot, then
`test byte [di+I_KIND], KIND_PKG / mov bx,[di+I_SPTR] / call mem_free_owner_x`.

So after a re-home to Y:

* the loader's **region** is owned by the slot — it is one claim and it is the
  one to free;
* the **carve** holding the parts is owned by **X, the loader's segment** — and
  once `I_SPTR` is Y, the teardown sweep looks for Y and never finds it.
  **The carve would leak for the session, and it is the largest claim the
  package holds.**

That is the one real bookkeeping job: **re-stamp `MC_OWN` from X to Y**, one
walk of `mem_tab`. It cannot collide with the slot-owned records because a
slot is 0..11 and a segment is above the heap base.

---

## 4. The fences, and the one that breaks

Everything keyed to the package's segment was audited. All of it is written in
step 9 or later and therefore describes Y for free:

| reads the segment | when | after re-home |
|---|---|---|
| `I_SPTR` / `I_SIZE` | step 9 | Y — correct |
| the instance name, `inst_set_name_x` | step 9, off `[ld_base]:16` | Y's header |
| the icon flag `LD_H_FLAGS` | step 9, off `[ld_base]` | Y's header |
| `inst_pkg_fence` — `ES == I_SPTR`, `AX < I_SIZE` | at `fsx_run` etc. | Y — correct |
| `inst_pkg_spawn` — `mov bx,[di+I_SPTR]` | worker spawn | Y — correct, and an entry proc cannot spawn anyway (`I_STATE` unpublished) |
| `wm_pkgcall` — `W_SEG` | every callback | see §4.2 |
| `mem_free_rec_x` — the teardown sweep | close | Y, once §3's re-stamp is done |

### 4.1 `mem_own` says NO to the re-homed program

`mem_own` (memory.inc:3683) asks *"does a live claim START at ES, and is it
owned by an instance slot?"* — via `mem_owner_of_x`, which matches `MC_SEG`
exactly.

**Y is inside the carve, not the base of it.** `op_seg` places part *i* at
`op_base + (slack + offset)/16`, and the head slack is the cluster alignment
(§20.12.2) — zero on a 512-byte-cluster floppy and non-zero on a hard disk.
So Y equals a claim base by luck on one volume and never on another.

`mem_own` is the fence on `OSAPI_MEM_CLAIM`, `_CLAIM_HI`, `_CLAIM_DMA`,
`OSAPI_MEM_FREE`, `OSAPI_MEM_REGROW` and `OSAPI_MEM_MOVABLE`. **A re-homed
program could not claim a byte of memory**, which is fatal, and it would fail
*after* a successful launch, at whatever moment the program first asks — a
refusal that names memory and points nowhere near the cause.

> **BUILT, and it is TWO arms rather than one — SPEC.md 50.3.4 is the
> contract.** What this section missed is `mem_own`'s own standing requirement:
> *"it has to answer during the ENTRY PROC, which is where an app sizes
> itself"*, and the entry runs at `ld_start` step 8 while `I_SPTR` is published
> at step 9. So `inst_of_seg` cannot answer for the re-homed program's **own
> entry proc** — the same refusal this section is about, one step later.
> `cmp bx, [ld_base]` is the other arm and costs 4 bytes: `[ld_base]` is the
> kernel's word for *the segment the package being launched runs at*, cleared
> on both the success and abort paths (§66.6.1). Measured: **20 bytes of
> `.cold`, 0 of `.text`**.

The fix is **a routine that already exists**. `inst_of_seg`
(instance.inc:600) answers *"which live package instance is running at this
segment?"* by walking `inst_tab` for a matching `I_SPTR`. `mem_own` gains one
arm at `.no`:

```
    mov bx, es
    call inst_of_seg            ; is ES some live package's own segment?
    jnc .yes                    ; ...then it IS a package, by definition
```

That is not a widening of the fence — `I_SPTR` **is** the kernel's definition
of "this package's segment", and asking it directly is a narrower question
than the claim-base proxy that stands in for it today. It also makes
`mem_own` return `BX = ES` on that path, so the program's claims are owned by
Y and 3's teardown sweep frees them. The two halves agree.

`mem_own` is `.cold` and `inst_of_seg` is `.text`, so the call is far — but it
needs **no new wrapper**: `cw_mem_disp` is the generic one (`call bp / retf`,
kernel.asm:6337) and `retf` leaves the flags alone. With BP and DI banked the
arm is **~16 bytes of `.cold` and none of `.text`**.

### 4.1.1 It is NOT the containment arm, and that one would hang

`claude/skies-size-investigation` a652b61 built a different arm for a
different question — `mem_own_in`, **69 bytes of `.cold`**: *"does some claim
CONTAIN ES, and is that claim's owner itself a package?"*, returning the
**primary's** segment. It exists so that a part running with **its own DS**
can be identified, which is what a far-called code part needs.

**It does not solve this case, and ported as-is it does not terminate.** Trace
the re-homed program: nothing starts at Y, so `.no` is reached; `mem_own_in(Y)`
finds the carve and returns its owner, which 3 re-stamped to **Y**; the arm
then recurses into `mem_own(ES=Y)`, which reaches `.no` again, and again. The
commit's own termination argument is *"a primary's own claim STARTS at it, so
the call below always terminates on the arm above"* — and **the re-home is
exactly what breaks that invariant**. The other two stampings do not hang but
do not work either: to the instance slot, the recursion asks about segment
0..11 and is refused; left at X, X has just been freed and is inside no claim.

So the two arms are complementary, not duplicates, and this design needs only
the cheap one. If containment is ever wanted back, **`inst_of_seg` must come
first in `.no`** — which also repairs the termination proof on a stronger
footing than the original, because the inner call then ends either at
`mem_owner_of_x` (a claim starts there) or at `inst_of_seg` (it is a
registered `I_SPTR`) and can reach neither twice.

### 4.2 What the loader may not do

Only two things stamp a segment *before* step 9:

* **a window** — `wm_create` takes `W_SEG` from ES, and every later callback
  is far-called at `{W_SEG, PKG_DISP}`. A loader that creates a window would
  leave the kernel calling back into freed memory.
* **a worker** — already impossible from an entry proc.

So the rule is one line: **a re-homing loader creates no window; the
program's entry proc does.** The kernel can enforce it for free — the entry returns BX, and a
re-homing entry must return BX = 0. That is a `cmp`/`jne` on a path that
already tests CF.

Everything else an entry proc might do is stamped by **slot**, not segment —
the sound grant (`snd_inst`, set to the record around the call at
loader.inc:1005), the XMS release record, the toast — and survives untouched.

### 4.3 Movability — the carve becomes a REGION, and a region moves

This was raised as *"the freeing of the 2 KB loader RAM depends on everything
being movable — else it is fragmented by what we just loaded"*, and the answer
has three parts. **None of them costs a byte** and the first two are measured
rather than argued.

#### 4.3.1 The two claims come in by OPPOSITE DOORS, so neither can fragment the other

`ld_alloc` reserves the loader's region with **`mem_claim_hi_x`** — top-down,
*"away from the data claims growing up from the bottom"* (loader.inc:906).
`op_claim` takes the carve with plain **`OSAPI_MEM_CLAIM`** — bottom-up. They
are at opposite ends of the arena by construction, not by luck.

Measured, MSEG (the parts fixture) launched on a 5150 with 640 KB, GLaBIOS,
CGA, off `build/os8088-360.img` + `build/mseg360.img`:

```
===== MSEG loaded (region + carve both live) =====
arena 1B80..A000  (530 KB)
  1B800..1C400     3.0K  inst 0            movable  bottom-up
  1C400..1D800     5.0K  purge:MED/FATW1   PINNED   bottom-up
  1D800..1E400     3.0K  kern:ASC          movable  bottom-up
  1E400..1FC00     6.0K  purge:TRIV/WSAVE0 PINNED   bottom-up
  20000..2FC00    63.0K  purge:HIGH/02     PINNED   bottom-up  dma-head 4032 para
  2FC00..33800    15.0K  seg 9F00          PINNED   bottom-up   <- THE CARVE
  9F000..A0000     4.0K  inst 1            PINNED   top-down    <- THE REGION
  free runs: 1FC00 +1.0K, 33800 +430.0K
```

The region is the **top 4 KB of the arena with nothing above it**; the carve is
**430 KB below it**. Freeing the region does not leave a hole — it extends the
one free run that is already there. Closing MSEG (which frees both records
through the same `mem_free_rec_x` the re-home would) returns the heap to
**byte-identical free runs**: `1E400 +7.0K, 2FC00 +449.0K`, the map before the
launch, asserted equal.

Nor can anything wedge itself in between. `loader_run` is UI-task-only and one
load at a time (loader.inc:886), so no second top-down claim can be taken
between our region's claim and its free.

**So the free does not depend on movability at all.** Even in the worst shape —
a driver mounted at the ceiling above us — the freed block is the *most*
reusable place on the heap, because `mem_claim_1`'s `.hi` arm starts at
`[mem_top] - size` and walks down, so it is the first thing a top-down claim
looks at.

> **Do not read more into the door split than it holds.** It was a workaround
> for a heap with **no region compaction** — keeping the immovable thing out of
> the movable thing's way and minimising the damage when it could not be
> avoided — and §66.6.1 removed the premise. §50.3 and §50.3.2 have been
> corrected to say so; the sentence *"a package region can NEVER move, because
> its base IS its CS"* was the founding one and is what sent this plan looking
> for a defensive pin. With regions movable the split *mostly stops mattering*:
> a hole either end is a hole the compactor can close. What survives is a
> preference — a CS-based claim costs its holder's proc plus
> `mem_region_reloc`'s walk to move where a data claim costs a `rep movsw`, so
> high still keeps it out of the busiest traffic, and there is a fair argument
> that a carve which BECOMES an executable segment belongs high for that
> reason. It is not load-bearing here and §4.3.3 is why: the carve is safe
> where it is because it is a region, not because of which door it came in by.

#### 4.3.2 What the tree actually declares movable — the concern was RIGHT

*"Everything except modules should be movable now"* is correct, and
`docs/HEAP-CLAIMS.md` line 137 said otherwise (`every package region | PINNED
(forever) | base is CS`) — a row left behind by **§66.6.1**, which opened that
door and shipped. It has been corrected in place; the region table 80 lines
below it was already right and lists **eighteen movable regions**, every C
package's among them (`crt0.asm` declares it at entry with `cc_regreloc`).

`mem_can_move`'s pins, in the order it tests them:

| pin | reaches us? |
|---|---|
| `MC_RLOC == 0` — **nobody declared it** | yes, and it is the default |
| `mem_in_xfer` — a disk read is landing in it | no, transient |
| a **purgeable cache** — shed instead, never moved | no |
| `MEM_K_MOD` — an on-demand **module** | no |
| an instance slot whose region is not **frameless** | this is the arm we land on |
| a driver image with a live vector / nested frame / armed chip | no |

So movability is **opt-in per claim** and the default is still pinned — which
is the only part of the picture that differs from the concern as stated.

#### 4.3.3 A re-homed carve is a region in every sense the compactor tests

This is the finding that matters, and it means **the re-home needs no new
mechanism and no defensive pin.** Once `mem_reown_x` (§3) stamps the carve's
`MC_OWN` from the loader's segment to the **instance slot**, every fence
already lines up:

> **CORRECTED BY THE BUILD, and the correction is the important half.**
> `mem_is_region` tests `MC_SEG == I_SPTR`, and with a non-zero head slack
> those DIFFER — the claim's base is the carve's and `I_SPTR` is the part's, a
> cluster alignment apart (§20.12.2). So a re-homed carve is **not** a region
> in that sense, `mem_rr_tab` would **not** rewrite `I_SPTR` on a move, and it
> must stay **pinned**. Stamping `MC_OWN` with the instance SLOT is what
> guarantees it: `mem_find_own` matches the caller's segment or the claim's
> base, and a slot is neither, so `OSAPI_MEM_FREE` and `OSAPI_MEM_MOVABLE`
> both refuse. SPEC.md 20.12.10.5 carries the whole picture, including the
> **second shape** — a 512-byte-cluster volume gives a zero slack, the program
> sits at the base, and then everything below IS true of it. Both are
> coherent; only the first one needed protecting.

* **`mem_can_move`** takes `cmp bx, INST_MAX / jb` → `mem_is_region`, which is
  `I_KIND & KIND_PKG` **and** `MC_SEG == I_SPTR`. After the re-home both hold,
  so the carve reaches `mem_frameless` — **the same arm SHEET's and every C
  package's region takes today**.
* **`mem_frameless`**'s three questions all answer correctly for free. In
  particular `cmp bx, [ld_base] / je .nope` pins it exactly while the program's
  own entry proc is running, because §5.4's arm loops back to step 8 with
  `[ld_base]` = the carve. That is not a coincidence to be preserved — it is
  the word doing its job on the new base.
* **`mem_rr_tab`** already names every kernel word that would go stale:
  `wm_wins + W_SEG`, **`inst_tab + I_SPTR`**, and `mem_tab + MC_OWN` (the
  program's own later data claims are owned by its segment). The `I_SPTR`
  staleness this plan worried about is rewritten by a table row that has been
  there since §66.6.1.
* **`mem_reloc_call`**'s fourth arm dispatches the holder's own proc through
  `PKG_DISP` at the **new** base — and part 0 carries a package header, so
  `PKG_DISP` is there to be dispatched through.
* **`mem_find_own`**'s widened fence is the door: `cmp dx, bx / je .out`, *"a
  record whose base equals the caller's own segment can be nothing but that
  caller's region"*. For a re-homed program DX (the carve's base) **is** BX
  (its segment), so `OSAPI_MEM_MOVABLE` reaches it with **zero kernel change**.

A re-homed program is therefore movable on exactly the terms any other package
is: it declares `OS88_REGION_MOVABLE` if it hires no worker, and
`OSAPI_TASK_RESTARTABLE` on top if it does. Nothing here is a special case,
which is the point — the re-home hands the kernel a region, and the kernel
already knows what a region is.

One free consequence: the carve stops counting against the program's
`MEM_OWNER_MAX`. While the loader holds it the carve is one of the *loader's*
eight; re-owned to the slot it is nobody's data claim, so the program gets all
eight of its own.

---

## 5. The design

Naming, so the rows are countable: the **image** is the loader — the first
thing in the `.o88`, the thing that used to be the only thing. The **parts**
are 0..N after it. For Clear Skies that is an image plus three parts, and the
program is part 0.

### 5.1 The loader

```
    OS88_HEADER 'SKIES', sh_entry, OS88_F_ICON | OS88_F_PARTS
%include "os88parts.inc"
    OS88_PARTS_BEGIN 3
      OS88_PART OP_SEG,   OP_COMP       ; 0  THE PROGRAM - an ordinary package
                                        ;    image, header and all, padded with
                                        ;    its own bss (5.3)
      OS88_PART OP_ASSET, OP_COMP       ; 1  the artwork
      OS88_PART OP_ASSET                ; 2  the locations, packed per world
    OS88_PARTS_END
sh_entry:
    call op_load
    jc .out
    call op_handoff                     ; part 0 is the program; every other
    jc .out                             ; part's segment goes in its bss (5.2)
.out:
    xor bx, bx                          ; NO WINDOW: the program makes it
    ret
```

**Measured, this exact shape: 1,267 bytes of image**, 1,219 of it the parts
standard (§20.12.9's gating: `OP_COMP` only, no XMS, no lazy, no scratch, no
optional). Plus `OP_BSS`'s 86 — so ~1,353 bytes, and claims are KB-granular
(`mem_claim_1`: *"AX = KB wanted (1..640)"*), which makes it a **2 KB claim
and 2 KB back**.

It is also 1,267 bytes that **cannot** be compressed on the disk, against
10,736 for the shape built in the previous round — the part table is
file-relative and lives in the image, so the image is the one thing `PKGZ`
can never touch (§20.12.3). Everything else becomes an `OP_COMP` part.
**~9.5 KB of the file moves from uncompressible to compressible**, and the
engine, which packs at 81%, is most of it.

### 5.2 The handoff — and why it needs no mechanism at all

`op_seg` is an **accessor, not a load-time routine**. It reads `op_table`,
which is in the loader's image (§20.12.3), and `op_base`, `op_slack`,
`op_first`, `op_runkb`, `op_t_n`, `op_zn` and `op_optok`, which are in the
loader's bss. **Freeing the loader destroys both the code and the data behind
"where is part N".** A program that wants its artwork at run time — which
Clear Skies does, and its locations too — has to be told before the loader
goes.

Three ways were considered.

**A magic block.** The program's image carries `db 'O88PVEC'` and a row of
words; the loader scans the program's first page for the tag and fills them
in. This has a real precedent — `os88pkg.py` finds `op_table` in an image by
scanning for `'O88PARTS'` — and costs ~25 bytes of loader and no build-time
coupling.

**A packer stamp.** The program exports the vector's offset; `os88pkg.py`
reads it out of a map at pack time and writes it into a word of the *loader's*
image. The loader then does `mov di, [sh_vec]`. ~10 bytes of loader, but it
couples the two halves through the build: the packer must map the program to
pack the loader.

**Neither is needed.** The program's own header says where its image ends
(`LD_H_IMG` at +8), and its bss starts there. So the vector goes at **the head
of the program's bss**, the loader computes the address in three instructions
from a field it is already reading, and the program declares those words first
in its bss chain like any other. No tag, no scan, no stamp, no map, no
build-time coupling — and about eight bytes of loader:

```
op_handoff:                     ; ES = the program's segment, from op_seg
    mov di, [es:LD_H_IMG]       ; ...its bss begins where its image ends
    mov al, 1
    call op_seg                 ; part 1: the artwork
    mov [es:di], ax
    ...
```

**This only works because the kernel does not zero that bss**, which is §5.3,
and that is what makes the arrangement a design rather than a trick: the
program's bss arrives already zeroed *from the part*, so the loader's writes
into the head of it are the only non-zero thing there and they survive.

### 5.3 The program's bss ships inside its part, and §51.1.2 already decided this

The program's part carries `image + bss` bytes: the trailing zeros are in the
file. That is exactly what `tools/os88drv.py` does for a driver, and §51.1.2
is the reasoning, arrived at by the same route —

> *"The answer is to make the two the same number… What that costs is zeros
> back on the floppy, and on a packed disk it costs nothing measurable — a run
> of zeros is what LZ4 is best at. Measured: the 360KB system disk is 255 of
> 354 clusters with the strip and 255 without."*

— and the part is `OP_COMP`, so the zeros cost the disk nothing here either.
It also spares the loader an `OP_ZERO` row, which would turn `OP_HAS_ZERO` on
and put `op_scrub` and its arms back into an image that has just been measured
at 1,267.

§51.1.2 also supplies the one thing that must not be skipped: **a bound test.**
*"The header is a FILE: a foreign tool may write any `DRV_H_BSSP` it likes."*
Y's header is a part, read off a disk, and the same is true of it — so
`OSAPI_PKG_REHOME` takes **AX = the bytes available at DX** (the loader knows
it: it is the part's own `len`), and the kernel refuses when Y's
`image + bss` exceeds it.

### 5.4 The kernel side

A new X cell, `OSAPI_PKG_REHOME` — **DX = the program's segment, AX = the
bytes available there**, ES = the caller's (the fence). It banks both and
returns; it does nothing else, because the loader is still executing and
nothing may move under it.

`ld_start` gains one arm between steps 8 and 9:

```
    ; --- 8a. THE RE-HOME ---------------------------------------------------
    cmp word [ld_rehome], 0
    je .reg                     ; the ordinary launch, untouched
    <refuse a second one - once per launch>
    <BX must be 0: a re-homing entry owns no window (4.2)>
    mov es, [ld_rehome]
    xor si, si
    call ld_hdr_ok              ; the .o88 prologue's five tests, on the part -
    jc .abort                   ; already written, already .cold, already near
    <ld_img / ld_bss / ld_ent from Y's header; ld_need = img + bss>
    <refuse if ld_need > [ld_rehsz] - 5.3's bound test>
    mov bx, [ld_base]           ; --- the carve and every other claim the
    mov dx, [ld_rehome]         ;     loader made, re-owned X -> Y (3)
    call mem_reown_x
    call ld_slot                ; --- and the loader's own region, freed: it
    mov dx, [ld_base]           ;     is owned by the SLOT, so this is one
    call mem_free_x             ;     claim by base and not a sweep
    mov ax, [ld_rehome]
    mov [ld_base], ax
    mov word [ld_rehome], 0
    jmp .call8                  ; ...straight to step 8's far call
```

**It jumps to step 8, not step 7**, and that is load-bearing: step 7 is the
`rep stosb` that zeroes the bss, and on this path the bss arrived zeroed in
the part with the loader's handoff vector written into its head. Zeroing it
would erase the one thing the program is waiting to read. It is also fewer
kernel bytes than looping through step 7 would have been.

`mem_reown_x` is `mem_free_owner_x` with one instruction changed — the same
`cli`-bracketed walk of `mem_tab`, storing DX into `MC_OWN` where it matches
BX instead of clearing `MC_SEG`. It belongs beside it in memory.inc.

### 5.5 The tooling side

`tools/os88pkg.py` gains two things and no format:

* **the pad** — a part declared as the program is padded to `image + bss` from
  its own header, §51.1.2's strip in reverse;
* **one check** — that part is a valid v3 package image, and **its header's
  name and icon flag match the loader's**. The Disk window draws the *file's*
  header before any of this runs (`ld_icon`), and step 9 registers the
  *program's*, so two different readers see two different headers and they
  must agree.

## 6. The bill

Estimated, with the comparable each figure is taken from.

| where | what | bytes |
|---|---|---:|
| `.text` | the table cell — `OSAPI_XCELL` is 8 bytes exactly | 8 |
| `.text` | its `call COLD_SEG:..._x` thunk, as `osapi_mem_movable`'s | 5 |
| `.bss` | `ld_rehome` + `ld_rehsz` (words) + `ld_rehomed` (byte) | 5 |
| `.cold` | the slot body: `mem_own` fence, bank DX and AX, refuse a repeat | ~34 |
| `.cold` | `ld_start`'s arm above, the bound test included | ~80 |
| `.cold` | `mem_reown_x`, measured against `mem_free_owner_x`'s 20 | ~22 |
| `.cold` | `mem_own`'s `inst_of_seg` arm (4.1), through `cw_mem_disp` | ~16 |
| | **total resident** | **~165** |

Against the tree as it stands: `.text+.bss` has **8,407** left of
`KERN_CODE_MAX`, and only 13 of this lands there; `.cold` has 457 left in its
current rung and this is ~148; the footprint has 17,408 spare. **No rung is at
risk**, which is stated because it is true and not because it is the argument —
CLAUDE.md's rule is that the price of a byte is a byte, and this is 150 of
them.

### 6.1 What they buy

* **The loader's claim, per instance.** 1,267 bytes of image plus `OP_BSS`'s
  86, which is a **2 KB claim** at the allocator's KB granularity — measured,
  on Clear Skies' own shape (5.1).
* **The parts standard leaves the program's segment entirely.** 800 bytes
  today for a plain consumer (§20.12.9) — and, more to the point, the program
  stops being charged for its own loader at all.
* **The program is an ordinary package again.** No gates, no `op_seg`, no
  second segment discipline, no `cs:` on its own tables. For Clear Skies that
  is the difference between the engine being a far-called part and the engine
  simply being the program.

### 6.2 What it does not buy

The **carve is the same size** — the program and its bss occupy exactly the
memory they occupied as parts. This is not a memory-saving change of any
scale; it is ~1 KB of heap and a clean segment. Anyone hoping for more should
read §6.1's third bullet as the actual prize.

---

## 7. Refusals and alternatives

**The kernel doing the loading itself** — a flags bit meaning "the real image
is part N". That is the design
docs/plans/completed/O88-MULTISEG-PLAN.md §1 built to five waves and threw
away at **2,560 resident bytes**. Re-home is ~150 because the *package* does
every hard thing (sizing, claiming, reading, expanding) and the kernel only
re-points at the end.

**Shrinking the loader's claim instead of freeing it** (`mem_regrow` to
nothing) — the same bookkeeping plus an allocator path, and it leaves a claim
record occupied out of `MEM_MAX`.

**Making the program its own claim** so `mem_own` accepts it without §4.1's
arm — `op_load` deliberately makes ONE claim because `MEM_OWNER_MAX` is 8
(§50), and a second one for the program would be a second thing to free and
to relocate. §4.1 is eleven bytes and answers a better question.

**Letting the loader survive and just not run** — that is today.

**A magic block or a packer stamp for the handoff** — both were costed and
both are unnecessary; 5.2 has the reasoning and the eight bytes that replace
them.

**Porting a652b61's containment arm** — 69 bytes of `.cold` for a question
this design does not ask, and it would recurse for ever on a re-homed
program. 4.1.1 is the trace.

---

## 8. Risks, in the order they would bite

1. **§4.1.** Without the `mem_own` arm this ships and fails at the first
   `OSAPI_MEM_CLAIM` a re-homed program makes — which for most packages is
   after the window is up. Build the arm first, with its own gate.
2. ~~**Compaction.**~~ **CLOSED TWICE — see §4.3.3, which is the better
   reason.** The first close was that the carve is pinned by default
   (`mem_can_move`'s first test is `cmp word [ss:si+MC_RLOC], 0 / je .pin`, and
   nothing declares a carve movable), so the compactor stops before
   `mem_frameless` or `[ld_base]` is consulted at all. That is true and it is
   the *weak* answer, because it closes the risk by making the feature
   unavailable — and it left the question of what happens the day somebody
   declares one. §4.3.3 answers that: a re-homed carve is a **region** by
   `mem_is_region`'s own test once `MC_OWN` names the instance slot, so it
   moves under exactly the rules SHEET's region moves under. `mem_rr_tab`
   rewrites `I_SPTR`, `mem_reloc_call` dispatches through `PKG_DISP` at the new
   base, and `[ld_base]` pins it while its entry proc runs. **So do NOT add a
   defensive pin** — the ~6 bytes sketched for forcing `MC_RLOC = 0` in
   `mem_reown_x` would buy nothing and would cost the feature.
3. **`ld_unreserve` on an abort after the re-home.** It frees by slot and by
   `[ld_base]`; the arm sets `[ld_base] = Y` before anything can fail, so the
   carve is swept. Worth a red-run test rather than an argument.
4. **A loader that returns a window.** 4.2's `BX = 0` check makes it a
   refusal rather than a callback into freed memory.
5. **Y's bss overrunning its part.** Y's header is a FILE and a foreign tool
   may write any `LD_H_BSS` it likes - SPEC.md 51.1.2's own warning, one
   format along. `OSAPI_PKG_REHOME` takes AX = the bytes available at DX and
   the kernel refuses `image + bss` past it (5.3).
6. **A part that arrives short.** The handoff vector lives in the program's
   bss, which arrives IN THE PART rather than being zeroed by the kernel
   (5.4). The parts standard already guarantees the run arrives - `op_want`,
   `op_bend` and `op_unpack`'s own expansion check - but this is the first
   thing that depends on that being true of the TRAILING bytes, which is
   where SPEC.md 20.12.2's `op_tail` arithmetic lives. Worth a gate that reads
   the vector back rather than an argument.

---

## 9. The gates this needs

* **`t_rehome` (fast)** — host-side: the loader's header and the program
  part's header agree on name and icon; the program part is a valid v3 image;
  and it is padded to `image + bss` (5.3).
* **`rehome` (soak)** — a `tests/rehome/` package in mseg's shape: the loader
  loads, re-homes, and the program's window title says so. Then read the
  guest: `I_SPTR` is the program's segment, the loader's claim is **gone from
  `mem_tab`**, the carve's `MC_OWN` is Y, **the handoff vector in the
  program's bss names the other parts** and their bytes are what they should
  be, and a claim the program makes afterwards **succeeds** — that last one is
  4.1's whole gate and must go red without the arm.
* **`rehomeclose` (soak)** — close it and assert `mem_avail` returns to what it
  was before the launch. The leak §3 describes is invisible until you look.
  Measured on MSEG today, the free is exact: the map after a close is
  **byte-identical** to the map before the launch (§4.3.1), so this row asserts
  equality and not a tolerance.
* **`rehomemove` (soak)** — **BUILT**, and it is indistinguishable from
  `tests/regmove.py`'s subject in every respect but one: a re-homed program has
  a word of its OWN to fix, the handoff naming a part that lives inside the
  block being moved. `tests/filler` forces the pass; five assertions; and the
  break-it run corrected the gate itself — see SPEC.md 20.12.10.5.1.
* **`rehomeabort` (soak)** — **BUILT**, §8 risk 3's red run: the re-homed entry
  refuses itself with the loader already freed and the carve on a slot, and the
  heap has to come back byte for byte (SPEC.md 20.12.10.6.1).

---

## 10. Sequencing

0. **`op_want`'s double subtraction**, which is a live bug on this branch and
   has nothing to do with the rest of this. `op_bend` is the run's exact byte
   end and `op_claim` subtracts `op_tail` from it again.

   **It costs no kernel bytes to fix and it makes every consumer smaller.**
   `os88parts.inc` is package-side, and the fix is a REMOVAL — the `op_tail`
   measurement in `op_size`, the subtraction in `op_claim`, and the bss word.
   Applied and measured: **a plain consumer 800 → 775, Clear Skies' loader
   1,219 → 1,194, `mseg` with all five features 2,581 → 2,552.** Twenty-five
   bytes back for every package that uses parts.

   **A build-side gate is not a substitute**, and the wrap is the lesser half.
   On a run big enough not to wrap, `op_want` is asked for up to 511 bytes
   FEWER than it should be — and `op_want` is the running count `op_read`
   decrements as bytes arrive, the one thing that says the run was not short.
   Refusing tiny payloads in `os88pkg.py` would leave that hole open in every
   real package; the fix closes it and pays 25 bytes to do so.

   a652b61 has the fix and the reasoning; port it first and separately.
1. **`mem_own`'s arms** — **BUILT** (SPEC.md 50.3.4). Two, not one, and that
   is the finding: `inst_of_seg` covers the program's life after step 9, and
   `cmp bx, [ld_base]` covers the **entry proc**, which step 9 has not reached.
   `mem_own`'s own header states that requirement (*"it has to answer during
   the ENTRY PROC, which is where an app sizes itself"*) and §4.1 missed it —
   with only the `inst_of_seg` arm a re-homed program would have been refused
   memory at exactly the moment §4.1 says an app sizes itself, which is the
   same defect one step later. Measured against HEAD by building both arms into
   private trees: **20 bytes of `.cold`, 0 of `.text`, 0 of `.bss`** (estimate
   was ~16 for one arm).

   **AND IT CANNOT BE GATED ON ITS OWN.** This says *"with a gate"* and there
   is no gate to write, by construction: both arms grant ownership only to a
   segment the kernel has already RECORDED as this package's — `[ld_base]` or
   `I_SPTR` — and nothing writes either of them to an inner part until step 3
   below lands. A fixture that swaps DS to an arbitrary segment inside its own
   carve and calls `OSAPI_MEM_CLAIM` is **still refused, correctly**, so it
   cannot go red for the right reason (docs/WRITING-TESTS.md §1). What that
   fixture *can* gate is the negative: that the fence was **not** widened into
   a containment test, which is §4.1.1's trap. So steps 1–4 land together, and
   the arm ships behind the rows in step 4 rather than a row of its own.
2. `mem_reown_x`, and the `MC_OWN` re-stamp — **BUILT** (SPEC.md 50.4.1), and
   the new owner is the instance **SLOT** rather than the program's segment,
   which §4.3.3 had the wrong way round. It is what makes the carve
   unreachable to the program, and it has to be: `mem_rr_tab` rewrites
   `I_SPTR` by matching the OLD BASE, and with a non-zero head slack `I_SPTR`
   is the part's segment where the claim's base is the carve's.
3. `OSAPI_PKG_REHOME` (0x0530) and `ld_start`'s step 8a — **BUILT**
   (SPEC.md 20.12.10). Measured against HEAD: **`.text` +14, `.bss` +4,
   `.cold` +166 = 184 resident**, against §6's estimate of ~165.
4. `tests/rehome/` and `tests/rehome.py` — **BUILT**, one row at 360KB. It
   found a real defect on its first run: the arm did not clear `[ld_rehome]`,
   so step 8a re-fired on the way back and re-homed the program to itself
   until `wm_create` ran out of window slots. And it was **broken on purpose**
   to earn step 1 its gate — with `mem_own`'s arms disabled the title reads
   `REHOMED 3/4 BA`, check 3 alone, which is §50.3.4's defect exactly.
   **`rehomemove` and `rehomeabort` are BUILT too** (SPEC.md 20.12.10.5.1,
   20.12.10.6.1). `rehomemove` runs at 1.44MB because the zero-slack shape is
   the only one that can accept the declaration, and it found that its own
   signature check would false-pass — a compaction does not scrub what it
   copied from. `rehomeabort` is the same source built `-DRH_ABORT` and covers
   §8 risk 3, which asked for *"a red-run test rather than an argument"*.
   **`rehomeclose` was NOT built and should not be**: the close-and-compare is
   already an assertion of the `rehome` row, and a second row running the same
   launch to the same comparison is a green row that tests nothing
   (docs/WRITING-TESTS.md §1).
5. `os88pkg.py`'s header agreement check.
6. Only then, a real consumer.
