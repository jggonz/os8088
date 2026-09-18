# WHAT NAVIGATION COSTS, AND THE SLOT THAT MAKES IT COST THAT

**STATUS: OPEN. The finding is measured on ONE package and the audit is not
done.** It is written now because it was found while planning the DOS box's
path wave (docs/plans/DOS-EXEC-PLAN.md), and deviating to it there would have
been the tail wagging the dog.

**§4 carries a correction in place** - this document first refused the cached
location outright, on §18.9.3's disk-swap argument, and that refusal was
wrong. The kernel banks nine FAT sectors against a boot-sector signature
today (§18.8.2); refusing a directory cluster on evidence the kernel trusts
the FAT to was inconsistency rather than caution. The refusal is kept beside
the correction because the reasoning that produced it is the reasoning
somebody will produce again.

---

## 1. The finding

**TANK ATTACK freezes for about six seconds to save a 40-byte high score
file**, measured by the fork owner. `apps/tank/tkhs.inc` walks
`SYSTEM` -> `APPDATA` on its own volume and back, and it does it with
`OSAPI_FILE_GOTO`.

There are **three** GOTO slots and only one of them is for navigating:

| slot | what it does |
|---|---|
| `OSAPI_FILE_GOTO` | **a REMOUNT** - real floppy I/O, then a directory scan, a sort and an icon harvest, because a Disk window is about to DRAW this folder |
| `OSAPI_FILE_GOTO_Q` (§19.2.2) | quiet. Inside the volume you are on it is **a WORD, no I/O at all**; crossing volumes it keeps the BPB and the FAT window and skips the scan, the sort and the harvest. But it moves the GLOBAL cwd and not the instance's |
| `OSAPI_FILE_GOTO_QM` (§74.1) | GOTO_Q's quiet stand **and the instance moves with it**, so the next file cell resolves there |

So the six seconds is not the cost of walking a directory tree. It is the cost
of asking, four times, for a folder to be prepared for DISPLAY by a program
that is not going to display it.

## 1.1 ...and the slow slot does not only pay, it FLUSHES

This is the half that makes six seconds make sense, and it was found by asking
whether os8088 needs a `BUFFERS=`-style directory cache. **It has one.**

SPEC.md §19.2.3: the directory walk reads through a cached window -
`MEM_P_DIRW`, a 16KB purgeable claim holding **eight runs** of directory
sectors, keyed on **the volume index plus `[dsk_sigcur]`**, the
position-sensitive signature of the boot sector that mount read (§18.8.2). A
switch to another volume and back keeps the runs; a disk swapped for a
different one loses them. It is *"is this the same disk?"* followed by a
memory operation, built and measured: metadata `int 13h` on a reference copy
went **50 to 22**, a 2.3x.

`dsk_dotdot_x` reads through it too, so climbing a `..` chain a second time is
memory rather than revolutions. (§19.2.3 said it was deliberately not a caller;
that paragraph was stale and is corrected there.)

**And then the bullet that indicts `OSAPI_FILE_GOTO`:**

> **A FULL mount drops every run and a quiet one does not.** A full mount is a
> navigation or a **Refresh**, and Refresh answered out of a cache is a no-op,
> which is the one thing it must never be. A quiet mount (§18.9) is a volume
> switch inside an operation, and that is exactly the case worth keeping.

`OSAPI_FILE_GOTO` is a full mount. **So each of Tank's three calls does not
merely spend twelve sectors of its own - it throws away the machine's entire
directory cache on the way past.** The cost lands on whatever runs next, too,
which is why this is worth more than a per-package saving. `GOTO_QM` inside a
volume is a word, and across volumes a quiet mount, and a quiet mount keeps
the runs.

So the conversion in §5 buys three things per site and only the first was
costed here originally: the remount, the scan/sort/icon harvest, **and the
cache the remount was about to discard.**

## 1.2 WITHDRAWN: the batch bracket is not one of Tank's costs

**This section claimed a third cost and it was wrong.** `tests/pathcost.py`
measures six same-volume `OSAPI_FILE_GOTO_QM` calls at **0 reads** - §19.2.2's
*"inside the volume you are already on it is a WORD, no I/O at all"*, measured
rather than quoted. The boot-sector re-read `OSAPI_BATCH_BEGIN` elides is per
**volume switch**, and `apps/tank` walks inside one volume.

So Tank has the TWO costs this plan named first - the display mount, and the
flush of §19.2.3's eight cached runs - and `GOTO_QM` cures both. The bracket
is still unused by every walker in the tree and is still right for a copy or
an install that crosses volumes; it is simply not this.

The section is kept rather than deleted because the reasoning was careful and
still wrong, and reading how is worth more than not having said it.

### 1.2.1 The original section, kept as it was written

`OSAPI_BATCH_BEGIN` / `OSAPI_BATCH_END` (§18.9.3) are published, and **no
walker in the tree calls them**. Inside the bracket `dsk_bpbok` = 2 and a
floppy reuses its banked BPB instead of re-reading **LBA 0 at every volume
switch** - the SDK's own figure is *"one call and one revolution per switch,
and was 41 of one install's 199"*.

It matters here because `dsk_here_ok` can only answer "no-op" when the caller
is **already standing at that exact cluster**, which a walk never is. So every
level of an unbracketed walk re-reads the boot sector to recompute
`[dsk_sigcur]` - one seek to LBA 0 and one revolution, per `..`, on top of
everything else in §1.1.

So `apps/tank`'s save has THREE separate costs and this plan originally named
one:

| | what it costs | the cure |
|---|---|---|
| the display mount | 12 sectors, a scan, a sort, an icon harvest | `GOTO_QM` |
| the cache flush | all eight of §19.2.3's runs, hurting whatever runs next | `GOTO_QM` |
| the boot sector | one seek to LBA 0 and one revolution PER LEVEL | the batch bracket |

The bracket **nests and cannot be left open** - any `gfx_unlock` ends it - so
taking it around a save is safe by construction, and a walk holds no gfx lock
so nothing inside one ends it early.

docs/plans/PATH-WAVE-PLAN.md §3.2 is the full account, and its conclusion
reaches back here: the correct sequence is three slots deep and no package
assembles it, which is an argument for a kernel-side walker rather than for
better advice in a comment.

## 2. Tank's own comment is the worked example, and it is not a mistake

This is worth quoting because it shows exactly how the wrong slot gets chosen,
and the author reasoned correctly from what they had:

> **It is OSAPI_FILE_GOTO and not its quiet twin.** GOTO_Q moves the GLOBAL
> cwd and deliberately not the instance's, while FILE_FIND, _READ and _WRITE
> all resolve in the INSTANCE's folder - so a quiet move is undone by the very
> next call, and the save writes nothing at all while the load appears to work.

Every word of that is true **about `GOTO_Q`**. `GOTO_QM` is the slot that
answers it, and the SDK describes Tank's case in as many words: *"wrong for a
program whose working folder changes on every call"*. `QM` arrived for RunCPM,
where a CP/M drive is a folder, and nothing went back to tell the packages
that had already refused `Q` for the right reason.

**So the lesson for the audit is that a slow call site may be a CORRECT
refusal of the wrong alternative**, not carelessness - which is why this is a
per-site read and not a `sed`.

## 3. It is a pattern, not a package

`call OSAPI_FILE_GOTO` appears at roughly **twenty sites in eleven files**:

- **`apps/os88type.inc`** - four, and this is the one that matters most,
  because it is a SHARED INCLUDE: Word, CWord, Font Viewer and Audio all carry
  it. Its `.back` site is a pure restore (*"put the caller back where it was,
  on every path out of here"*) and wants `QM` exactly.
- `apps/skies/csset.inc` three, `apps/tank/tkhs.inc` three,
  `apps/dotdel/ddhs.inc` two, `apps/word` two, `apps/scribe` two,
  and one each in `apps/sheet`, `apps/frotz`, `apps/audio`, `apps/texpad`.

Eight packages walk to `SYSTEM\APPDATA` (§19.9): The Wire, FTPD, Clear Skies,
Tank, Dot Delirium, Cyclone and the two that reach it through `os88type.inc`.

**Some of those sites are RIGHT.** A Save As that is about to show a folder
wants the listing, the sort and the icons - that is what the slot is for. The
audit's question per site is *"is this program about to draw this folder?"*,
and only a No converts.

## 4. The cache is NOT refused - it wants the evidence the kernel already has

**An earlier revision of this document refused the cache outright, and that
was wrong.** It is kept here as a correction rather than deleted, because the
reasoning that produced it is the reasoning somebody will produce again.

The refusal quoted §18.9.3: a floppy can be swapped, no predicate the kernel
can evaluate answers it, so the assertion is the caller's under a frozen UI
and any unlocking of the interface ends it. Every word of that is true **of
the batch bracket**, which is a stricter thing than this: the bracket asks to
skip boot-sector re-reads *entirely* for the duration of an operation. A
package that wants to stand in `SYSTEM\APPDATA` again is not asking to skip
validation. It is asking for validation that does not cost a walk.

**And the kernel already has it.** §18.8.2: `dsk_bpb_sig` computes a 16-bit
signature of the boot sector **the mount has already read** - rotated between
adds, so position-sensitive rather than a plain checksum - and
`dsk_fatw_pick` reuses a banked FAT window only when that signature matches
the one banked with it. `[dsk_sigcur]` is the signature of the volume mounted
now.

So the kernel **stakes nine cached FAT sectors on this evidence today**. A
wrong answer there writes file allocations out of another disk's FAT, which
is a far worse outcome than a misplaced high score. Refusing a directory
cluster on evidence the kernel trusts the FAT to is not caution, it is
inconsistency.

### 4.1 What that makes the design

```
    banked:  (cluster, volume, signature)
    on use:  signature still matches  ->  GOTO_QM and go.  A WORD COMPARE,
                                          no I/O at all.
             otherwise                ->  walk once, re-bank.
```

That is *"we check once, then we stop thinking the user has just swapped
disks"* - with the check cheap enough that it can simply be made every time,
which is better than checking once, because it is also correct on the machine
where swapping floppies IS normal use: one drive, 128KB, a disk per program.

It needs **one published slot**: the current volume's signature. Something
like `OSAPI_VOL_SIG` - BL = a volume, out AX = its 16-bit signature, 0 = not
mounted. A few bytes of `.text` and one API cell, and then every package in
the tree can bank a location safely instead of each inventing a rule.

**A generation counter would be the wrong primitive.** A counter that moved on
every remount would invalidate constantly - volumes switch often and most
switches are the same disk coming back - so packages would re-walk for
nothing. The signature moves when the DISK changes, which is the question
actually being asked. It has the further nice property that two IDENTICAL
disks share a signature and also share the cluster, so a copy is not a false
invalidation.

### 4.2 What the signature is worth, stated honestly

Sixteen bits collide. Two unrelated disks can share a signature, and then a
banked cluster is trusted when it should not be. **That risk is exactly the
one the kernel already runs for the FAT window** - no better and no worse -
and the rule to carry is that a caller must not treat it as stronger than
that. For a high-score file the consequence of a collision is a corrupted
score file. A package banking something it cannot afford to lose should also
carry a witness (§4.3).

### 4.3 ...and §1.1 makes it very likely unnecessary

**The kernel already banks what §4.1 proposes banking, keyed on the same
evidence, for every package at once.** §19.2.3's window holds the directory
sectors and checks `[dsk_sigcur]` before trusting them; a package banking its
own cluster would be a second cache over the same data, with a second
invalidation rule to get wrong, saving the memory read the kernel's cache
already answers from.

So `OSAPI_VOL_SIG` probably should NOT be built. The order stands - convert,
measure - but the expected outcome has moved: with `QM` the walk is free
moves plus directory reads that a warm window answers from memory, and the
thing that was making it cold was the slot being converted.

What would change that is a number: if a converted walk is still slow on a
4.77MHz 8088, the question becomes *why is the window cold*, and the answers
are its capacity (eight runs, a working set of three volumes' roots plus
subdirectories) or its purge (it is `MEM_P_DIRW`, given back the instant
anything else needs the room) - neither of which a per-package cluster bank
would fix either.

### 4.4 The original "it may still not be needed" 

Measure before building any of it. With `QM` the walk is **two free cluster
moves and two directory sector reads**, because `QM` inside a volume is a
word. If that lands near Clear Skies' half-second then the bank buys little,
and the slot is worth having anyway for the packages that walk further.

If a bank IS built and the thing banked matters, the belt-and-braces shape is
a **witness**: stand there quietly and confirm a known name is present before
trusting it. One directory read instead of the whole walk, and it closes the
collision case as well.

**Clear Skies remains not a counter-example either way**: its parts live
INSIDE the file it already opened (§20.12), so it never navigates and has
nothing to invalidate. "Just being there" is the absence of a walk.

## 5. What this plan is waiting for

The DOS box's **path wave** produces the number. It walks directories with
these same slots and will establish what a level actually costs with `QM`,
on a 4.77MHz 8088, measured rather than reasoned. Doing this audit first would
mean doing it blind.

Then, in order:

1. **Convert `apps/tank/tkhs.inc`'s three sites** and measure the save against
   the owner's ~6 seconds. One package, one number, the whole case.
2. **Convert `apps/os88type.inc`'s `.back` site**, which reaches four packages
   for one edit and is an unambiguous restore.
3. **Audit the remaining sites** by §3's question, converting the Noes.
4. **Only then** decide whether §4.1's banked cluster has anything left to
   buy, and whether `OSAPI_VOL_SIG` is worth its cell on that evidence.

## 6. What is NOT decided here

- Whether any of this wants an SDK change. It may be that the fix is entirely
  per-package and the only durable artefact is a sentence in `os88api.inc`
  next to `OSAPI_FILE_GOTO` saying which of the three to reach for - which
  would be the cheapest useful outcome and should be considered first.
- Whether §47 applies. A program that cannot reach `SYSTEM\APPDATA` currently
  fails quietly in at least one package; that is a different bug and is not
  this plan's.
