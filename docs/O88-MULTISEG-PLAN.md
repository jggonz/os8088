# `.o88` PARTS — a standard packages embed, not a kernel feature

**Status: design, not started.** The ask, and the three goals it has to meet,
are §0. **§1 is the record of a first attempt that was built, measured and
thrown away** — read it before proposing anything that puts this work back in
the kernel.

---

## 0. The ask, and the verdict up front

> *"Right now every .o88 can only load a single segment. This means any
> programs that want more than one segment have to keep inventing ways to get
> around the problem; usually with a second file on disk that they load after
> they are running, reusing standards that the kernel has for completely
> separate (and more valid) reasons."*

Three goals, in the requester's words:

1. **Stop each package inventing a new way** to do what they are already
   doing — "with no or minimal modifications to the kernel";
2. **One file**, so a large application does not appear to the user as a
   scatter of files that a copy can separate;
3. **Load only a minimal amount**, so a package does not read for seconds and
   only then discover it is going to be refused.

**The verdict: this is a package-side standard with a three-fact kernel
change, and the whole kernel bill is ~52 bytes.**

The kernel learns exactly three things it does not know today:

1. a `*.O88` bigger than 64KB is still a package *(built — §5.1)*;
2. a package's `image` may be smaller than its file, when the header says so;
3. a package's entry proc is told the name of the file it was launched from.

Everything else — sizing, refusing, claiming, reading, XMS, loading on demand
— the package does for itself through slots that are **already published**,
and `apps/os88parts.inc` is where that is written once so nobody writes it
again. §4 is the mapping, capability by capability, and every row of it was
checked against the tree.

**There is no `.o88` v6.** The file stays a v3 package — same header, same
`image`/`bss`, same three-byte dispatcher — with one flag bit that says *"my
file is longer than my image, on purpose"*. What is behind that is the
package's own business, and the kernel never parses it.

---

## 1. The first attempt, and why it was thrown away

**Five waves of this were built, gated and committed** on branch
`claude/o88-multi-segment-plan-qcotof`: a `.o88` v6 with a kernel-parsed
parts block, a carve claim, per-part claims, `PF_XMS`, `PF_OPT`, `PF_ZERO`,
`PF_SEP`, `PF_LAZY` and `OSAPI_PKG_LOAD`. Seven test rows, every kernel
assertion verified to fail. It works. **It cost 2,560 bytes of resident
footprint** and that is why it is not the design.

| wave | what | cost |
|---|---|---|
| 1 | the mount and loader stop bounding the FILE by `APP_MAX_SIZE` | 17 `.cold` |
| 2 | the format, the tool, the carve, `dsk_read_chain`'s mid-cluster resume | 1,024 |
| 3 | ASSET / `PF_SEP` / `PF_ZERO` / `PF_OPT`, the named refusal | 512 |
| 4 | `PF_XMS` and the staging loop | 512 |
| 5 | `PF_LAZY` and `OSAPI_PKG_LOAD` | 512 |

**Every wave's estimate was low, and waves 4 and 5 were low by a factor of
four.** That is the first finding and it is about estimating, not about
parts: the old plan's XMS section priced that leg at ~86 bytes by costing the
*decision* — ask, fall back, claim a buffer, copy — and left out that the
staging loop is two loops, that a sector-rounded read and an even-count copy
round in opposite directions inside one chunk, and that a loop which spends
every register needs eight words of `.bss` to bank what it is doing. **"A
copy loop" is not a line item.** Nothing in the design below should be priced
that way; where this file gives a number it says how it was arrived at.

### 1.1 The module dead end

The first idea for getting the bytes back was SPEC.md §2.8's on-demand
kernel module: `.cold` code cut out of this build by `os88mod.py`, shipped as
a `.DRV`, read into a heap claim when the feature is asked for. It is a real
mechanism with two users already (`CTRL.DRV`, `FORMAT.DRV`), and 2,031 of the
2,278 `.cold` bytes were measured as movable into one.

**It fails on two counts, and each one alone is fatal.**

**It needs the system disk, and this is not an operation that has anything to
do with the system disk.** `mod_need` searches nothing — it goes to
`[dsk_bootvol]` and only there (SPEC.md §2.8.4). Consider the machine the
project calibrates against: one drive, booted, and the user has since put in
another disk. It need not be *our* apps disk; it need not have anything to do
with this kernel's version. **They still have to be able to run a program off
it.** A design that answers `Needs Sys Disk A:` there has refused a launch
for a reason that belongs to the kernel's own housekeeping.

**And it does not save the memory anyway.** `KERN_BUDGET` exists to protect
*heap* — it is what guarantees kern_big resides in 128KB at the desktop
(docs/KERNEL-MEMORY.md guard 1). A module that is never dropped is 2KB of
heap forever, and a module that loads the parts loader can never be dropped,
because the feature has no end of life. **2KB in the heap and 2KB in the
kernel image are the same 2KB.** The saving was an accounting artefact.

### 1.2 What the failure was really about

Both counts are the same mistake seen twice: **the kernel was made the owner
of a thing it has no stake in.** It parsed a block off a hostile disk, made
claims on somebody else's behalf, remembered where each instance's file was,
and carried a per-instance side table to do it — for a capability that only
some packages want, in a segment where every byte is billed to every machine.

The moment the loading moves into the package, all of that becomes the
package's own business, and the package is already holding everything it
needs to do it.

---

## 2. What exists today, and what the workarounds cost

### 2.1 The ceiling is the segment, and two shipped packages are near it

A package links at `org 0` and addresses itself with 16-bit offsets, so
image + bss can never reach 64KB whatever the heap has free (SPEC.md §20.1).
`APP_MAX_SIZE` is `0xF000` = 61,440 bytes. Measured on this tree:

| package | image | bss | image+bss | of the 60KB ceiling |
|---|---|---|---|---|
| `SHEET.O88`   | 47,919 | 3,137 | 51,056 | **83%** |
| `TRACKER.O88` | 17,291 | 31,861 | 49,152 | **80%** |
| `ARTFUL.O88`  | 18,365 | 21,703 | 40,068 | 65% |
| `TEXPAD.O88`  | 24,284 | 12,288 | 36,572 | 59% |

And two packages upstream are **at** the wall: `WEAVE.O88` is 61,408 of
61,440 — **32 bytes under** — and `LOOM.O88` 60,930. §12 is what those two
have already had to build for themselves and is the best statement of the
problem in the tree.

### 2.2 The four workarounds, and what each one reuses

| workaround | what it is | what it reuses that was built for something else |
|---|---|---|
| **`CWORD.OVL` / `RUNCPM.OVL`** (SPEC.md §73.14) | a second CODE segment out of the same build, far-called both ways | `OSAPI_FILE_READ` and the per-instance current directory (SPEC.md §19.2.1) |
| **`C64.ROM`** (docs/C64-SPEC.md `1.4`) | 20,480 bytes of KERNAL + BASIC + CHARGEN in a sidecar, read into a claim at launch | the file API, plus a hand-written "the disk is missing this" refusal |
| **RunCPM's CCP** (SPEC.md §74) | 2KB read from the launch folder at start and on every warm boot | the file API, plus a 2KB claim the package makes itself |
| **claim-at-entry scratch** (RunCPM's 64KB of Z80 RAM, C64's 64KB) | a large zero-filled working set claimed from the entry proc | `OSAPI_MEM_CLAIM` — which is exactly right, except the *refusal* lands after the whole package has been read |

### 2.3 What all four have in common

**The size question is answered after the program is already running**, and
**the second file is a real file** a copy can separate from its package.
`cc_ovneed`'s two-word stamp exists to catch a stale pair and catches only
*some* stale pairs, by its own admission.

Note what the table does **not** say: none of the four is reaching for
anything the kernel refuses them. Every one is using published slots
correctly. What they lack is a *convention*, and that is the shape of the
answer.

---

## 3. The constraints that still bind

Measured on this tree, and every one survives the redesign.

### 3.1 A disk call costs ~400 ms whatever it moves

One `int 13h` is 1–2 disk revolutions on the target machine. **Cost disk work
in calls, not in sectors** (PERFORMANCE.md). This is what makes goal 3 sharp:
a refusal that costs one extra read is a different thing from one that costs
none.

### 3.2 The claim map

| | value | where |
|---|---|---|
| `MEM_MAX` | **32** claim records for the whole machine | `kernel/memory.inc` |
| `MEM_OWNER_MAX` | **8** claims per owner word | `kernel/memory.inc:44` |

A package's own claims carry **the segment it runs in**, and eight of them is
the cap. **This binds the standard**: a package with six parts must not make
six claims. It makes one and carves it — which is what the first design's
carve already was, and it is now the package's arithmetic instead of the
kernel's.

`mem_free_rec` frees claims owned by the instance slot **and** by `I_SPTR`,
so a package's own claims are returned at teardown with no new code. That is
the single fact that makes this whole design safe to hand over.

### 3.3 `I_SPTR` is the identity and `I_SIZE` is bytes

`I_SIZE` is a 16-bit **byte** count of the region, summed by the Task
Manager's RAM column (SPEC.md §28). **The region never grows to hold the
parts** — it holds the primary alone, so `I_SIZE` keeps its unit and both its
bound tests. Parts are claims beside it. Unchanged from the first design and
still right.

### 3.4 XMS is 286+, an overlay, and the target machine has none

`XMEM.DRV` is loaded only on a machine with memory above 1MB (SPEC.md §41).
Extended memory is not addressable in real mode: every byte crosses
`OSAPI_XMEM_COPY`, ≤ 32,768 bytes a call, even counts, UI task only. **On
every 8088 there is no store at all**, so anything using it writes both paths
— which is the requester's own framing: *"XMS is a PLACEMENT HINT"*.

### 3.5 Disk: the 360KB apps floppy has nine clusters left

Embedding an asset makes a `.o88` bigger by exactly the asset. The demo does
not ship (§10.4) and `C64.ROM`'s disk is an on-demand build, so this design
spends none of the nine.

---

## 4. The architecture

**The kernel loads the primary segment, exactly as it does today, and hands
the package the name of the file it came out of. The package does the rest.**

That is the whole of it. The three goals fall out:

- **goal 1** — the rest is written once, in `apps/os88parts.inc`, and
  embedded by whoever wants it;
- **goal 2** — the parts live past the image in the same file, so a copy
  cannot separate them;
- **goal 3** — the part table is **compiled into the primary's image**, so by
  the time the entry proc runs the kernel has already read it and sizing
  costs **no disk at all**. A stub primary refuses in the time it takes to
  read the stub.

### 4.1 Every capability, mapped

This table is the design. Each row was checked against the tree.

| what the standard needs | published today? |
|---|---|
| resolve a filename on the volume and folder it was launched from | **yes** — SPEC.md §19.2.1: every name you pass the file API resolves in *your instance's* directory, which the kernel tracks per instance and which follows the user's dialogs |
| read its own file at an offset | **yes** — `OSAPI_FILE_READ_AT` (0x0358): `SI` = name, `ES:BX` = buffer, `CX` = capacity, `DX:AX` = offset; offset and capacity must each be a cluster multiple |
| learn the cluster size, to satisfy that | **yes** — `OSAPI_FILE_DFREE` returns `BX` = sectors per cluster |
| size itself before committing | **yes** — `OSAPI_MEM_AVAIL`: largest free run and total free, in KB |
| claim conventional memory **from the entry proc** | **yes** — `OSAPI_MEM_CLAIM`, documented as working there, "where you have no window yet" |
| claim immovable code away from the data arena | **yes** — `OSAPI_MEM_CLAIM_HI` |
| claim XMS, attributed to itself | **yes** — `OSAPI_XMEM_ALLOC`, and the loader already brackets the entry call with the instance stamp, so `inst_caller` answers correctly |
| move bytes through XMS | **yes** — `OSAPI_XMEM_COPY` |
| have every claim returned at teardown | **yes** — `mem_free_rec`, §3.2 |
| refuse the launch cleanly | **yes** — return CF=1 from the entry; the loader's abort path is `LD_EABORT` |
| say why, in the user's terms | **yes** — `OSAPI_TOAST` |
| **be a file bigger than 64KB** | **no** — §5.1, built |
| **have `image` smaller than the file** | **no** — §5.2 |
| **know its own filename** | **no** — §5.3 |

Three gaps. Two of them are one instruction each.

### 4.2 What the kernel no longer does, and why that is a gain

The first design put **542 bytes of validator** in the kernel, because a
parts block read off a disk is hostile input and the rule is that every byte
off a disk is treated as hostile.

**In this design there is no block on the disk to validate.** The part table
is the stub's own compiled data, inside the image the kernel has already read
and already bounds. The kernel's exposure goes back to exactly v3's — magic,
version, link base, dispatcher, `image`, `bss` — and the 542 bytes
**evaporate rather than move**. A corrupt file corrupts the package that owns
it, which is where the damage belongs.

This is not a small point. It is the largest single line in the old bill, and
it is deleted by moving the table from the disk into the image.

---

## 5. The kernel's bill, itemised

| | bytes | how arrived at |
|---|---|---|
| **§5.1** the mount types a `*.O88` up to `PKG_FILE_HI`, and `ld_run_body` step 1 reads the size in 32 bits | **17** | **measured**, built, no rung crossed |
| **§5.2** header flags bit 3 relaxes `image == file size` to `image <= file size` | ~10 | one `test` and one `jnz` around a compare `ld_check_hdr` already makes |
| **§5.3** bank the launching file's display name, and hand the entry proc a pointer to it | ~28 | 13 bytes of `.bss`, a 13-byte copy out of `dsk_ent`, and `mov si, ld_lname` before the far call |
| **total** | **~55** | |

**Quote this as an estimate.** §1's finding stands: the two numbers above that
are not measured were arrived at by counting instructions, which is exactly
how the old plan under-priced two waves by four times. The ceiling the
requester set is **512 total kernel bytes**, and even at four times this
lands at ~220.

### 5.1 A file bigger than 64KB — built

`APP_MAX_SIZE` bounds the primary segment's image + bss, which is all it ever
meant; it bounded the FILE as well only because the two were the same thing.
SPEC.md §19.1 typed a `*.O88` as a package only when its size dword's high
word was zero. `PKG_FILE_HI` (SPEC.md §3) is the mount's bound at 1MB, and
`ld_run_body` step 1 tests the staged size's high word **before it trusts the
low one** — a 70,144-byte file's low word is 4,608, a plausible small
package, and sizing a region from it is a wrap that reports `Bad package`
about a file whose only fault is its size.

**Lifting the mount's rule alone is a defect**, and `tests/pkgbig.py` is what
catches it: two fixtures, and the pair is the experiment.

### 5.2 `image` smaller than the file

`ld_check_hdr` asserts `image == file size` on a v3 file — the truncated-file
guard. A package carrying parts is longer than its image by construction, so
the guard needs one exception, and the header's flags byte is where it goes
because `ld_check_hdr` already has that byte loaded.

**A flag bit and not a version bump.** The file *is* a v3 package: same
header, same contract, same dispatcher, and the kernel's relationship with it
is unchanged. What is past the image is not a format the kernel knows — it is
the package's own data, and a version number would advertise a kernel
capability that does not exist. An older kernel refuses the file because
`image != size`, which is the correct outcome by the correct route.

### 5.3 The package's own filename

The one genuinely new fact. A package can already resolve any name in its own
instance's directory (SPEC.md §19.2.1) — it simply does not know its own.

`ld_run_body` step 1 stages the directory entry through `dsk_get_dir`, whose
buffer `dsk_ent` is shared and is overwritten before step 9. So the display
name — SPEC.md §19.1's field at offset 0, NUL-padded, max 12 chars, already
in exactly the form the file API takes — is copied into 13 bytes of loader
`.bss` at step 1, and step 9 passes `SI` = a near pointer to it into the
entry call.

**`SI` is free there** and `ES` already names `KERNEL_SEG` by the callback
contract, so the package reads `[es:si]` and copies it into its own bss. It
is an *input* the entry proc may ignore, so every existing package is
unaffected.

**One buffer, not one per instance.** The kernel does not remember where each
instance came from; it tells the package once, at the only moment the package
needs telling, and the package remembers. The first design spent 60 bytes of
`.bss` on a per-instance cluster, drive and disk signature to do a worse
version of this — worse because a banked cluster goes stale on a disk swap
and a *name* re-resolves correctly through machinery the kernel already runs.

---

## 6. The standard — `apps/os88parts.inc`

The SDK include is the deliverable that meets goal 1. It is ordinary package
code, assembled into whoever embeds it, and it owes the kernel nothing.

### 6.1 The part table lives in the image

**As built** (waves 2–4; `OS88_PARTS_BEGIN` takes the row count, because the
table is fixed-size data the packer fills in place):

```nasm
    OS88_PARTS_BEGIN 5
      OS88_PART  OP_SEG                    ; 0  code, far-called
      OS88_PART  OP_ASSET                  ; 1  data
      OS88_PART  OP_ASSET, OP_ZERO, 64     ; 2  64KB zero-filled, no file bytes
      OS88_PART  OP_ASSET, OP_XMS          ; 3  above 1MB if there is any
      OS88_PART  OP_SEG, OP_LAZY           ; 4  fetched on first use (wave 5)
    OS88_PARTS_END
```

and what it publishes to the package: `op_load` (the whole of the entry's
first line), `op_seg` (a part's base segment, 0 = not there), `op_lin` (its
32-bit linear base when it went above 1MB), `op_read`, `op_size`, `op_claim`,
`op_scrub`, `op_xload` and `op_row`. `OP_XMS` parts come **last** — the span
that climbs has to be contiguous, and `os88pkg.py` refuses any other order.

`os88pkg.py` fills each row's file offset and length at pack time — the
assembler cannot know a separately assembled module's length — exactly as it
would have done for the old on-disk block, but into the **image**. The rows
are the package's own data at a label the package names, so there is no
placement rule, no `vec_off`, no 400-bytes-of-the-peek budget, and nothing
for the kernel to bound.

**Alignment is the one thing the file layout must respect.**
`OSAPI_FILE_READ_AT` wants a cluster multiple for both offset and capacity,
and cluster size varies by volume (a 360KB floppy's is not a 1.44MB one's).
Two candidate answers, and the choice is §11 wave 2's first job:

- **pad to 2KB**, a multiple of every cluster size this OS mounts — costs
  file bytes and nothing else;
- **ask `OSAPI_FILE_DFREE`** for sectors-per-cluster and round at run time —
  costs one call whose documented cost is O(clusters) of CPU (~105 ms on a
  20MB hard disk, far less on a floppy).

The second is more correct and the first is free. Measure both before
choosing; the answer may be "pad, and ask only when the pad was not enough".

**Wave 2 chose NEITHER, and the third answer is better than both.** Parts are
512-aligned in the file and the read simply **starts at the cluster boundary
at or below the run** — the head slack is a multiple of 512, hence of 16, and
a heap claim is paragraph-aligned, so the slack costs a segment adjustment and
nothing else. No padding, no `OSAPI_FILE_DFREE` call, and it is correct on a
volume with 32KB clusters that neither of the two candidates had met.
`tests/multiseg.py`'s 360KB row is the one that exercises it: 1KB clusters
against 512-byte alignment, verified to fail with the slack not added back.

### 6.2 What the stub does, in order

1. **Copy the name** the kernel handed it in `SI`.
2. **Size, from the table it already has.** Sum what the parts want; ask
   `OSAPI_MEM_AVAIL`. **No disk has been read at all** beyond the image the
   kernel already read — this is goal 3, and it is answered better here than
   the first design managed.
3. **Refuse, if it must** — `OSAPI_TOAST` naming the figure, then CF=1 out of
   the entry. The kernel's abort path tears down what exists and nothing is
   half-built.
4. **One claim, carved.** §3.2's cap is eight per owner word; a package with
   six parts makes one claim and computes each part's base inside it. Parts
   that must be separately placed (an `OP_XMS` one, or one the package will
   want to free early) take their own.
5. **Read**, `OSAPI_FILE_READ_AT` per contiguous run. Parts that share the
   carve share a read.
6. **Zero** the declared scratch tails.
7. **Fill its own vector** — a table in its own bss, at a label it named. No
   kernel involvement, no rule about where it may live.

### 6.3 Lazy is not a mechanism, it is a line

A part marked `OP_LAZY` is simply one the stub does not read at step 5. When
the package wants it, it calls the same helper with the same table row, and
the helper claims and reads it then.

**Built in wave 5, and it stayed one line**: `op_size`'s loop tests the flag
and jumps to `.next`, so the row is in no total and outside the carve.
Everything else is three small routines around that — `op_fetch` (claim, read,
bank), `op_drop` (free, unbank) and `op_lazyok` (would it fit, asked without
claiming, for §47's greying). See SPEC.md §20.12.4 for the four rules
`os88pkg.py` enforces and why the fetched segment lives in the row's own `zkb`
word.

**The whole of `OSAPI_PKG_LOAD` disappears** — an API slot, 237 bytes of
kernel body, 157 for re-staging the table out of the package's image with
every bound re-taken against `I_SIZE`, 59 for seeking the chain, 40 for
checking the disk had not been swapped, and 62 bytes of `.bss` banking a
cluster and a signature per instance. All of it existed to let the *kernel*
re-find a file on the package's behalf. The package re-finds it by name,
through the per-instance directory the kernel already maintains, and a disk
that has been swapped out comes back as an ordinary file-API error the
package reports in its own words.

**Idempotence** — the contract `cc_ovneed` needs, since it asks on every
entry into an overlay (SPEC.md §73.14) — is a non-zero test on the package's
own vector slot.

### 6.4 What the standard costs the package

Estimated at **500–700 bytes** of image, against the 2,031 that came out of
the kernel, because the validation is gone (§4.2) and the file-finding is
gone (§6.3). Two instances of one package carry two copies — but two
instances already duplicate the whole image, so it is proportionate.

**This is the trade, stated plainly:** a machine running two multi-part
packages holds two stubs where the old design held one kernel copy. A machine
running none holds nothing, where the old design held 2,560 bytes. The
requester's stated preference is the second machine, and every machine this
project targets is closer to it.

---

## 7. Identity — the rule that does not move

**The entry proc, every window, every callback and the worker task live in
the PRIMARY segment.** A part holds code and data reached by far call and
**does not call the kernel's ES-fenced slots** — `OSAPI_MEM_CLAIM` and its
relatives, `OSAPI_MEM_FREE`, `OSAPI_TASK_SPAWN`, `OSAPI_FSX_ENTER`. The
primary claims on a part's behalf and passes it a segment.

That rule is what leaves every kernel test of "the package's segment"
untouched — `mem_own`'s fence, `mem_free_rec`, `inst_pkg_spawn`'s identity
test and `I_SIZE` bound, `wm_destroy_seg` at teardown, `wm_pkgcall`'s
`W_SEG:12`, `fsx`'s ownership test and `ld_icon`. **Not one of them moves**,
and that was true of the first design too. It is the one piece of it that
transfers without change.

It is also not a new discipline: it is what SPEC.md §73.14's overlay already
does, where only code moves and `DS` stays the package's.

---

## 8. What this design cannot do

Stated up front rather than discovered.

- **The kernel cannot report a parts failure.** A package that cannot build
  itself refuses from its entry proc and the user sees the package's own
  toast, not `ld_status`. That is a loss of one consistent sentence and a
  gain of a specific one.
- **The kernel cannot account for a part in the Task Manager's RAM column
  separately from the package.** It never could — a package's own claims
  already carry its segment, and this makes parts the same kind of thing.
- **A part cannot be `PF_OPT`-greyed by the kernel** (SPEC.md §47's door).
  The package greys its own row, which is where the knowledge of what the
  part was for lives anyway.
- **Eight claims per package** (§3.2). The carve makes this a non-issue in
  practice, and it is the reason the carve is in the standard rather than
  optional.
- **`OSAPI_FILE_READ_AT` is UI-task context**, like every file slot. A worker
  may use a part another task loaded; it may not be the one that loads it.
  The first design enforced this in the kernel and it now enforces itself.
- **The stub cannot verify it read its own file** rather than a same-named
  file on a disk that was swapped. Nor could the first design, honestly: it
  banked `[dsk_sigcur]`, which is computed once per *mount* and goes stale on
  a swap with no intervening mount. What a package can do that the kernel
  could not is put a magic and a build stamp at the head of each part and
  check it — `cc_ovneed`'s idea, and now available to every package for free.

---

## 9. Deliberately not done

- **No relocation and no compaction of a part.** Claims are pinned by default
  (SPEC.md §66) and a part is code or a table the package holds a segment
  for. `OSAPI_MEM_MOVABLE` is available to a package that wants to opt in and
  write the relocation proc; the standard does not do it for them.
- **No paging service.** A window of an XMS asset mapped into a conventional
  buffer on demand is a cache with an eviction policy, and the requester has
  already said the app manages the copy past the initial load.
- **No second kernel-visible file format.** The point of the design is that
  there is not one.

---

## 10. The demo package

`MSEG` — a test package, not shipped, for §10.4's reason.

### 10.1 What it must exercise

1. **Two SEGMENT parts**, far-called, each answering a value only it can
   compute;
2. **an ASSET** the primary reads through its own vector;
3. **a zero-filled scratch part** with no file bytes at all, verified all-zero
   and writable to its last byte;
4. **an XMS part**, verified by pulling it back through `OSAPI_XMEM_COPY` —
   and verified to have fallen back to conventional on an 8088;
5. **a part the machine must refuse**, with the launch carrying on without it;
6. **a lazy part**, absent at launch and fetched on demand;
7. **the 360KB geometry**, whose clusters are not the 1.44MB disk's — the
   case §6.1's alignment question is really about, and which passes by
   accident at one geometry.

Every one of these was built and gated in the first attempt, and the host-side
rows survive nearly intact: they read a verdict string out of the package's
own segment, which is a place the redesign does not move.

### 10.2 Three independent checks per part

A segment number on the screen proves a claim was made and **not** that it was
filled. The first attempt's rig is the right one and carries over:

- the **signature** the primary reads out of the part's own bytes;
- a **far call** to `<part>:0` answering a value only that module computes;
- the module **summing its own data area with a rotating add** against a
  figure the assembler computed over the same generated bytes — a plain sum
  passes on a transposition, which is what a misaligned read actually
  produces.

### 10.3 The refusal twin

A second package identical but for one impossible part, so that the refusal
path is exercised on its own. The first attempt learned two things here worth
carrying: **512KB was granted** on the 640KB XT and the row passed on a
mechanism it had never run, and **a refused load is not required to leave the
heap unchanged**, because `mem_claim` sheds purgeable caches and retries.

### 10.4 Why it does not ship

It is a capability gate, not software. `make mseg` builds it and no shipped
floppy carries it, like `wire.o88` (SPEC.md §78.9).

---

## 11. The plan, in waves

Each wave ends buildable, gated and mergeable.

| wave | what | gate |
|---|---|---|
| **1** | **BUILT** — §5.1, standing alone: `PKG_FILE_HI`, the mount's type rule, the loader's high-word guard. **17 bytes of `.cold`, measured**, no rung crossed | `pkgbig` (soak) and `make test-full` |
| **2** | **BUILT** — §5.2 and §5.3, and the whole kernel change is now in: **`.cold` +70, `.bss` +15** measured on the tree it landed on — 85 bytes across waves 1+2 against the 512 ceiling, crossing one cold rung there and none on the branch it was written on (§11.0). `os88pkg.py` appends parts and fills the table in the image; `apps/os88parts.inc` is the standard; `MSEG` is the consumer | `multiseg`, `mseg360`, both verified to fail |
| **3** | **BUILT** — `OP_ZERO` scratch, `OP_OPT`, and the one claim that holds the run and the scratch together. **ZERO kernel bytes**: `kernsize` is identical to wave 2's, which is the architecture's whole claim made good | `msegnomem` — the refusal measured at **9 sectors against a successful launch's 21**, and the heap byte-for-byte untouched |
| **4** | **BUILT** — `OP_XMS`: one block above 1MB for the whole span, climbed through a transient conventional claim, and a fallback to an ordinary filed part where there is no store. **ZERO kernel bytes again** | `msegxms` on QEMU — MartyPC is an 8088 and cannot host extended memory at all (docs/TESTING.md's closed list, entry 6) — verified to fail, and it caught two defects on its first green run |
| **5** | **BUILT** — `OP_LAZY`: a row `op_size` steps over, `op_fetch`/`op_drop`/`op_lazyok` around it, and the fetched segment banked in the row's own `zkb` word. **ZERO kernel bytes for the third wave running** | `mseglazy` — the carve provably ends before the lazy part, and the disk goes on the KEY |
| **6** | **BUILT** — the real consumer (§11.1): `apps/c64`'s 20,480-byte ROM sidecar becomes part 0 of `C64.O88`, and the C SDK learns parts on the way. **ZERO kernel bytes for the fourth wave running** | `c64part` — and it had no existing gates to lean on, so it has one now |
| **7** | **BUILT** — both baselines re-blessed, docs/KERNEL-MEMORY.md's entry written and its stale figures re-derived, CLAUDE.md's row current. This file is the design record | `make test-full` |

**Wave 2 is the wave that decides whether this design is right**, because it
is where the kernel change lands and is measured. If §5.2 and §5.3 come in
anywhere near 38 bytes, everything after it is package code and costs the
kernel nothing at all. If they do not, stop and say so before wave 3.

**It came in at 85, and waves 3 to 7 added nothing.** The whole design is
`.cold` +70 and `.bss` +15 against the 512 bytes the requester set — and the
five waves after the kernel change, which between them brought scratch parts,
optional parts, extended memory, lazy parts, the C SDK's own support and the
first real consumer, are byte-identical to wave 2. What it COST depends on the
tree: on its own branch the cold rung had 89 bytes free and it crossed
nothing; landing on `elendilon-new` that rung had 55, so the union crosses and
`KERN_SIZE` steps once — 31 steps of spare to 30, far inside the four-step
standard, and no raise is asked for. `docs/KERNEL-MEMORY.md` carries the
entry; that is move 28's sentence at rung scale.

### 11.0 What wave 2 measured

**The kernel change is `.cold` +70 and `.bss` +15** — 85 bytes across waves 1
and 2 together against the 512 the requester set.
§5's estimate was 55; the miss was 2×, not the 4× §1 records for the old
design's last two waves, and it is worth naming what was left out because it
is the same *kind* of omission both times — **plumbing that the feature
implies but the feature description does not mention**:

- **the file size had to become 32 bits on both resolve paths.** `ld_fsz` was
  one word. A package carrying parts can be bigger than a segment, so
  `ld_res_name` (the name path) and `ld_run_body` step 1 (the index path)
  both had to store and bound the high word, and wave 1's `LD_EBIG` at step 1
  had to move down into `ld_check_hdr` where the flags byte can decide.
- **step 6 was reading the FILE.** `mov ax, [ld_fsz]` — which is the image on
  every v3 package and always was, so it had never mattered. With parts it is
  the difference between reading the primary and reading the whole file into
  a region sized for the primary. It is one word changed to `[ld_img]` and it
  costs nothing, but it was not in the estimate because nobody had asked what
  step 6 measured.

`ld_check_hdr`'s new test is exact rather than merely relaxed: **without bit 2
a 32-bit size whose low word happens to match the image is a truncated file,
not a match**, and it now says so instead of accepting it. That hole was open
before this wave and had nothing to do with parts.

**Both gates were verified to fail.** With the kernel's bit-2 exception
disabled MSEG comes back `ld_status` 2, `Bad package` — and the row's own
diagnostic names that case. With `op_seg`'s head slack not added back, the
**360KB row answers `MSEG 0/3 BAD` and the 1.44MB row still answers `MSEG 3/3
OK`**, which is what makes them two rows rather than one run twice.

**And the 360KB row was passing without testing anything, for one build.**
MSEG's image was two sectors, so its first part landed at file offset 1,024 —
cluster-aligned on *both* geometries, so `op_claim`'s slack was zero
everywhere and the arithmetic the row exists for never ran. The image is
padded to three sectors now, and `op_slack` is asserted (512 at 360, 0 at
1.44MB) so it cannot quietly stop being true. This is the third time in this
project's history that a fixture has had to be made *deliberately awkward* to
keep a row honest, and the cheap guard each time was to assert the awkwardness
rather than the outcome.

**`tests/pkgbig.py` had the same disease and it came back with wave 1.** It
was running 1.44MB media on a 360KB-drive machine and passing anyway, because
both of its refusals are decided from the directory entry's size dword and
neither fixture is ever opened — so nothing asked the drive for a sector it
has not got. The fix was made once on the discarded branch and went with it;
it is on `os8088_5150_herc_gla_144` now. **A fix that lived only in work that
was thrown away is a fix that has to be made again**, and that is the cost of
the restructure that this file should record rather than the reader
rediscover.

### 11.0.1 What wave 3 measured

**Zero kernel bytes.** `kernsize` after wave 3 is byte-identical to after
wave 2 — scratch parts, optional parts, the one-claim carve and the whole of
their arithmetic are package code. That is the architecture's central claim
and this is the first wave that could test it.

**The refusal moves 9 sectors; a successful launch moves 21.** Measured with a
`DISKCNT` kernel, on the same disk, with both images padded to the same seven
sectors so everything before the moment of decision is identical, and with the
odds against the claim: the refusal goes first on a cold volume and the
success second on a volume the first launch warmed. The margin is `op_read`'s
13-sector carved run — the one thing the refusal never asks for.

**It was `dsk_dbg_i13` and it should not have been**, which is the correction
wave 4 forced. The row asserted *at most two `int 13h` calls — the launch's
own peek and image read*, and it held while both images were three sectors.
Wave 4 padded them to five, which put `MSEG`'s image across LBA 36 — a
cylinder boundary at 1.44MB — so the driver split one run into two and the
count went to three, for a launch that had not read one extra byte. Worse,
the same coalescing makes the refusal and the success cost the **same** three
calls while moving 12 sectors and 21, so the comparative half of the assertion
was about to become unsatisfiable for a reason that has nothing to do with the
design. `dsk_dbg_sec` counts what was read; the call count was counting where
the file happened to sit. (PERFORMANCE.md prices disk work in *calls* because
a call is what a revolution costs — that is a claim about TIME, and this row
is a claim about what was read at all.)

**And the heap is byte-for-byte untouched across a refused launch**, which is
strictly stronger than the kernel-side design could assert. That design *tried*
the claim and let `mem_claim` refuse it — and `mem_claim` sheds every
purgeable cache and retries (SPEC.md §50.6.2), so asking for 640KB threw away
the machine's caches before answering; its row had to settle for "no claim
owned by a slot holding no live record". `op_load` asks `OSAPI_MEM_AVAIL`
first, and a question costs nothing. **Verified by A/B**: made to try the
claim instead of asking, the table goes from 4 claims to 2 and the row names
exactly that.

**Three things this wave found.**

- **`ld_unreserve` already frees what an entry proc claimed.** Its own comment
  says so — "the slot for the region, `[ld_base]` for whatever the entry proc
  claimed before it failed" — and it sweeps XMS by record too. The teardown
  this design needs was written for the case this design *is*, before anyone
  had this design. Nothing new was needed and nothing leaks.
- **A package's own toast does not survive an abort.** §21 step 10 toasts
  `[ld_status]` over `fm_stattab` for every outcome including `LD_EABORT`, and
  it runs after the entry returns — so `op_load`'s `Not enough memory` is
  replaced by the kernel's `Load failed`. **This is a real loss against the
  kernel-side design**, which named the figure itself. The mitigation is that
  a package which can run degraded (an `OP_OPT` part refused) does not abort,
  and its toast stands; one that cannot has to survive to draw a window if it
  wants to explain. Recorded rather than worked around.
- **An overflow wave 2 would have shipped.** `op_cap` is a word and the head
  slack is added to it, but the slack is not known until the volume is — so a
  64KB run on a 32KB-cluster volume overflows it. `op_size`'s 128-sector bound
  could not see that; `op_claim` refuses on the carry now, and the comment
  says which bound is which.

**`OP_OPT` is all-or-none, and only on scratch.** Dropping optional parts one
at a time is a search whose order the author cannot predict, and a rule they
can is worth more; a file-backed part cannot be optional at all, because it
sits inside the carved run and dropping it would split the one read the carve
exists to be. `os88pkg.py` and the `OS88_PART` macro both refuse the
combination, so it fails at assembly rather than at launch.

### 11.0.2 What wave 4 measured

**Zero kernel bytes, again.** `kernsize` after wave 4 is byte-identical to
after waves 2 and 3 — `.cold` +70, `.bss` +15
against the branch point, which is the whole of the kernel bill for the
design. Extended memory turned out to be the easiest wave to say that about:
`OSAPI_XMEM_CAPS/ALLOC/FREE/COPY` have been published since §41 and the
standard is only a caller of them.

**And it needed no teardown of its own.** §21 step 9 already brackets the
entry call with the instance's stamp (SPEC.md §41.5.1), so a block claimed
from `op_xload` is attributed to the instance rather than to `XM_OWN_KERN`,
`xm_release_rec` frees it at close, and `ld_unreserve` sweeps it if the entry
then refuses. That is the same shape wave 3 found for conventional claims:
the paths this design needs were written for the case this design *is*.
`tests/msegxms.py`'s fourth assertion exists because getting it wrong is
**silent** — a block nobody frees looks exactly like a block nobody claimed,
and it would be stranded for the session.

**The gate is on QEMU and the rule is not "it was easier".** MartyPC is an
8088; there is nothing above linear 0x0FFFFF for it to host, so this is
docs/TESTING.md's closed list entry 6 in its cleanest form — there is no
"prefer MartyPC" to weigh, because MartyPC has not got the hardware. It
borrows `tests/xmcheck.py`'s boot and its `xm_tab` reader for the same reason
that file exists.

**It caught two defects on the way to its first green run, and both were
invisible on every other row.**

- **`op_xload` walked the block's base as its copy cursor**, then restored it
  at the end by subtracting the BLOCK's size — where what the cursor had
  advanced by was the SPAN's. MSEG's span is 1,536 bytes in a 2KB block, so
  every part came back 512 bytes low: the bytes really were at 0x110000 and
  `op_lin` answered 0x10FE00. The package's own checksum said `MSEG 5/6 BAD`
  and the copied-down buffer was all zeros. The failure path was wrong the
  same way — `OSAPI_XMEM_FREE` was handed an address that was never claimed.
  The fix is a second word, `op_xcur`, and the base never moves; it is worth
  four bytes of the *package's* bss to make one of them mean one thing.
- **MSEG passed the copy direction in `DI`, which is also its part-loop
  counter.** `OSAPI_XMEM_COPY` takes 0/1 in DI; `ms_check` walks the parts in
  DI. Part 5 set it to 1 and the loop restarted at part 2 — **for ever**. The
  symptom was not a wrong answer, it was no window at all, `ld_status` still
  reading its boot value, and a desktop that looked fine because the clock is
  not drawn by the entry proc's task. A fixture bug rather than a standard
  one, but it is exactly the failure a machine-with-a-store row exists to
  find: on every 8088 that code path never runs.

**And it forced a correction to wave 3's instrument** — `dsk_dbg_i13` →
`dsk_dbg_sec`, for the reason §11.0.1 now carries. That is the second time in
this design that a *gate* was measuring something adjacent to its claim; both
times the tell was a number moving for a change that could not have moved it.

**One mirrored constant became one copy.** Both host-side gates addressed the
package's own bss as `image + OP_BSS` with `OP_BSS = 65` written down;
`op_xcur` took it to 69, and `tests/multiseg.py` then read `ms_seg[]` two
entries early and reported parts 0 and 1 as having no segment and the 600KB
optional part as GRANTED — while the window beside it said `MSEG 6/6 OK`. A
gate disagreeing with the package it is testing about the package's own
memory. `tools/os88parts.py` parses the equ chain out of
`apps/os88parts.inc` now, which is tools/os88geom.py's argument applied to the
SDK instead of to the kernel.

### 11.0.3 What wave 5 measured

**Zero kernel bytes, for the third wave running.** `kernsize` after wave 5 is
byte-identical to after waves 2, 3 and 4. Everything lazy needs — sizing
around a row, a claim, a read, a free — was already published; the standard is
only a caller.

**The lazy part is outside the carve, and that is what the row asserts.** MSEG
grew a seventh part, an `OP_SEG` module of 3,051 bytes — the biggest of its
five, on purpose, because what lazy buys is measured in the sectors the launch
did not move. Measured: the carve runs sectors 7..19 and part 6 starts at 20.

**A KEY fetches it, not the entry proc**, and that was a deliberate choice
about what is measurable. A fetch from the entry proc happens *during* the
launch, so its sectors are indistinguishable from the carve's; on a key they
are their own measurement — 9 sectors moved for a 6-sector part — and they are
also what a lazy part is FOR: a working set that arrives when the user asks
for the thing that needs it, and goes away again.

**How the A/B failed is the argument for the structural assertion.** With
`op_size` made to size a lazy row like any other, the carve runs to sector 25
and reads all six sectors of part 6 at load — and the *presence* assertion
does not notice, because `op_seg` answers a lazy row out of the row itself and
that word is still 0 until `op_fetch` writes it. So the package is told "not
here" about bytes it has already paid for. **Presence is what the package was
told; the carve is what the disk did**, and only a gate that reads
`[op_first]`/`[op_secs]` out of the guest can tell them apart.

**Two defects, and one of them was in the routine that exists to answer
honestly.** `op_lazyok` put the wanted KB in `BX` and then called
`OSAPI_MEM_AVAIL` — which answers in **two** registers, `AX` the largest free
run and **`BX` the total free**. So it compared a run against a total that is
`>=` it by construction and refused every time the heap held more than one
free block, which is almost always. A §47 greying helper that always greys is
worse than none: it is a wrong fact rather than an honest guess. The other was
the fixture's: MSEG's scratch check writes a `0x5A5A` probe at the end of the
claim to prove the size was real, and wave 5 made `ms_check` run again on
every key — so the second pass found its own probe and failed the zero scan. A
check that fails because the last run of the same check wrote to what it was
checking.

**Two mirrored layouts became none.** The gates addressed MSEG's own bss by
recomputing its equ chain in Python — `image + OP_BSS + PARTS*2 + 4 + 2 + ...`
— which is the fixture's layout typed out a fourth time, with the part count
in it. `tests/multiseg/msegsym.py` reads **nasm's own map** of the fixture and
refuses a map that is not byte-identical to the `.bin` that shipped, which is
`tests/xmcheck.py`'s `ovl_sym` discipline applied to a package. And the part
COUNT, written down as `PARTS = 6` in two gates, is now read out of the
package's part table (`tools/os88parts.py` decodes it), so growing MSEG does
not touch either row. `tools/os88pkg.py`'s own copies of the row stride and
the flag bits are checked against the include too now, through
`os88parts.PKG_MIRROR` — a packer writing rows at the wrong stride produces a
table the standard reads as garbage.

**And MSEG says WHICH part failed.** `ms_bad` is a bitmask beside the count,
set from a shadow of `ms_ok` one part behind. A `6/7` in a title otherwise
leaves the reader to reproduce three checks host-side to find out which row it
was — which is exactly what this wave spent a debugging round doing before the
bitmask existed.

### 11.0.4 What wave 6 measured

**Zero kernel bytes, for the fourth wave running.** The C SDK's support is
`apps/cc/crt0.asm` and five thunks; the consumer is a `%define`, a table and a
Makefile argument.

**THE C SDK NEEDED TWO THINGS AND BOTH ARE HANDLED IN crt0**, so a shim does
not have to know either. The standard's bss is an equ chain off a base that
was `os88_image_end` — the bytes past the image, which a C package does not
have, because SmallerC emits four sections and the loader zeroes a real
`.bss`. `OP_BSS_AT` is a `%define` now and crt0 reserves `OP_BSS` bytes down
there and points it at them; one symbol moves and the whole chain follows,
which is the only reason the standard needed no second version for C. And
`OS88_PARTS_BEGIN` emits data wherever the assembler happens to be, which in a
C package is whatever section the last `%include` left open — so
`CC_PARTS_BEGIN`/`CC_PARTS_END` put the table in `.data` and leave the
assembler in `.text`. `op_load` is called by `cc_entry`, first, because `SI`
arrives holding the kernel's name pointer.

**`os88_part_lin` is deliberately absent.** An `OP_XMS` part's base is a
32-bit linear address and this C dialect has no `long` (docs/C-TOOLCHAIN.md),
so it cannot be returned and a pair of halves would hand a C author two
numbers it has no arithmetic for.

**THE NUMBER THAT REFUSED THIS ON PAPER WAS THE WRONG NUMBER, and it had been
sitting in two files for a year.** `docs/C64-SPEC.md` §1.4 and
`apps/c64/rom/README.md` both recorded that an embedded ROM "would have been
about 73,000 bytes against SPEC.md §73's 61,440 cap — refused on paper, which
is what made the sidecar the design". `APP_MAX_SIZE` bounds the primary
SEGMENT's image plus bss, not the FILE. Measured after the conversion: image
**40,854**, bss **13,176**, sum **54,030** against that same cap, in a file of
**61,440** bytes. The cap was never the obstacle; until this design there was
nowhere in the format to put bytes that are not the image. **A sidecar is not
only a file that can go missing — it is a design somebody was talked out of by
a bound that did not apply.**

**What the conversion DELETED is the measure of it.** The port carried a whole
halted-machine state to say the ROM was missing: `c64_norom`, a four-line
notice on the glass with its own once-only gate and its own expose repair, a
permanent status row (`C64.ROM missing - see README.TXT` — 32 cells, which is
where §10.1's message-length cap came from), three greyed menu items, and
`build/c64uitest --no-rom`, a whole second host-test process that existed
because `os88_main` decides that surface once per launch. All of it deleted
rather than disabled, because a greying may not outlive its reason (SPEC.md
§47).

**And it found two things in the standard, one of them silent.**

- **A short run was not a refusal.** `op_read` stopped when the file ran out
  and wave 2 left the consequence to "the package's own checks on the parts" —
  which means every package inventing a way to tell a claim that was filled
  from one that was not, and C64 was about to grow exactly that. The standard
  can simply tell: it asked for bytes at a known offset and knows how many
  arrived. `op_read` now tracks what MUST arrive and refuses. **Verified**: a
  `C64.O88` truncated by one sector gives `ld_status 4` and no window, where
  before it would have loaded with 512 bytes of heap at the end of the KERNAL.
- **`op_size`'s sector cap was an off-by-one**, and silent in the worst way: a
  run of exactly 128 sectors passed the `ja` and `128 << 9` is 65,536, which
  in a word is **zero** — so `op_bytes` came out 0, `op_claim` asked for the
  head slack alone, `op_read` moved nothing, and the package was handed
  segments into a claim that was never filled.

**And two more that only a consumer could find.** `op_read`'s new "did it all
arrive" check computed the figure from the CARVE's words - so a lazy fetch,
which reads a different run entirely, demanded the carve's byte count and
refused every time. It is the caller's input now (`[op_want]`), because the
two reads measure different things and there is exactly one place that knows
which. And `apps/os88parts.inc` was not a prerequisite of a C package's
`.bin`: `make c64disk` re-linked a package whose runtime had changed under it,
and what caught that was `os88map`'s byte-identity check refusing to describe
the result - correctly, and several steps after the point where make could
simply have rebuilt it. Both are in `CC_RUNTIME` now.

**The gate is the third occurrence of one idea, so it was extracted.**
`tools/os88map.py` reads a package's own symbols out of nasm's map and refuses
a map that is not byte-identical to the binary that shipped — `os88sym.py`'s
rule for the kernel, `xmcheck.ovl_sym`'s for a driver overlay, and now a
package's. `tests/multiseg/msegsym.py` is three arguments to it, and
`tests/c64part.py` reads `_c64_m` and `op_base` out of a C package the same
way.

**And what the gate does NOT assert is worth as much as what it does.** The
obvious final assertion is that the KERNAL boots — `**** COMMODORE 64 BASIC
V2 ****` in the C64's own screen matrix, which nothing but the real KERNAL
executing out of the real ROM puts there. It is not asserted because **it is
not true before this change either**: measured by A/B against the unconverted
package, with the sidecar back and the conversion stashed, the 6510 runs
(cycles accumulate, `1% cpu`, no JAM) and never writes a byte of its own RAM —
zero page `$00/$01` is `0000` where the KERNAL's reset writes `$2F/$37` within
a few hundred cycles. That is the state of `apps/c64`'s core on this branch,
it is not this wave's to fix, and a row asserting it would fail for a reason it
does not name. What the gate says instead is exact: five 16-byte windows of the
ROM in the guest against the file, **including the last sixteen bytes of the
part**, because a carve one sector short reads perfectly at the front.

**A note for whoever owns this fork.** `apps/c64/rom/README.md` records a user
decision — *"use a sidecar that is loaded at runtime... instead of embedding it
in the package"* — and the runtime-loading half of it is exactly what the parts
standard does. The separate-FILE half is what this reverses, on the strength of
§11.1's argument and the measurement above. It is one `%define` in
`apps/c64/c64.asm` and one Makefile argument if it should go back.

### 11.1 Which consumer

**`C64.ROM` → one embedded ASSET.** 20,480 bytes of KERNAL + BASIC + CHARGEN
that today are a sidecar a file copy can separate from its package. The
before/after is the clearest of any candidate: a file that could go missing
becomes one that cannot, and the hand-written `C64.ROM missing - see
README.TXT` status row becomes a part the package either has or refuses over.
It is an on-demand build (`make c64disk`), so it spends none of §3.5's nine
clusters.

**Second choice, and arguably the better proof:** RunCPM's **64KB Z80 claim
as a scratch part plus the 2KB CCP as an ASSET** — it exercises both kinds
and moves a refusal from "after 43KB of package has been read" to "before a
sector was".

---

## 12. The customer, and the gap it found

**WEAVE and LOOM are what this exists for**, and reading them is what put
lazy parts in the design at all. Both are at the segment wall (§2.1). Both
already carry a hand-rolled version of this:

- `WEAVE.WSM` is read **only when the opened bundle declares a `<canvas>`**,
  and docs/WEAVE-SPEC.md is emphatic that a canvas-less bundle *"pays
  nothing: not a byte of heap, not a disk revolution"*;
- `LOOM.OVL` is **42,894 bytes** and is loaded on the first call into it —
  on a machine where a 256KB XT fits exactly one instance.

**An eager-only format is a worse deal than the sidecars for both**, which is
why §6.3 exists. In this design it costs the kernel nothing, which is the
strongest evidence that the architecture is the right way round.

They cannot be *this* work's consumer — this fork carries Weave at wave 2 and
their second segments arrive later — so they are the shape the standard is
checked against rather than the thing converted. Everything in §6 is cheap to
change now and expensive to change after packages embed it.

---

## 13. Decision record

| question | answer | why |
|---|---|---|
| kernel-parsed format, or a package-side standard? | **standard** | §1. The kernel version was built and measured at 2,560 resident bytes for a capability most machines never use |
| an on-demand module for the kernel version? | **no** | §1.1. It needs the system disk, which has nothing to do with running a program off another disk — and a module that is never dropped is the same bytes in a different account |
| a new `.o88` version? | **no, a flag bit** | §5.2. The file is a v3 package; what is past the image is not a format the kernel knows |
| how does a package find its own file? | **the kernel tells it its name, once, at entry** | §5.3. A name re-resolves through machinery the kernel already runs; a banked cluster goes stale |
| where does the part table live? | **in the primary's image** | §4.2. It stops being hostile input, which deletes 542 bytes of validator, and sizing costs no disk |
| one claim or several? | **one, carved, by default** | §3.2's eight-per-owner cap |
| does the kernel own part claims? | **no** | `mem_free_rec` already returns claims owned by `I_SPTR`, so teardown needs no new code |
| may an extra segment call the kernel? | **no** — §7 | the seven places "the package's segment" means something, none of which moves |
| is XMS a mode or a hint? | **a hint** | §3.4, and the requester's own framing |

---

## 14. Open questions for wave 2

1. **§6.1's alignment.** Pad to 2KB, or ask `OSAPI_FILE_DFREE`? Measure the
   `DFREE` call on a floppy before assuming the documented hard-disk figure
   applies.
2. **Does `OSAPI_FILE_READ_AT` refuse a capacity larger than the file's
   remainder**, or return a short count? The contract says it answers `DX:AX`
   = bytes delivered, 0 at or past the end — so a short final read looks
   answerable, but it has not been exercised at a part boundary.
3. **Does the entry proc's `SI` collide with anything?** It is free at the
   call site today (`BP` carries the entry offset, `DS`/`ES` are set) — but
   `wm_pkgcall` passes `SI` = the window pointer for *callbacks*, so the SDK
   documentation has to be unambiguous that this is the ENTRY only.
4. **What does `os88pkg.py` do about an image that is not a whole number of
   clusters?** The first part's offset has to satisfy §6.1, and `image` is
   whatever the assembler produced.
