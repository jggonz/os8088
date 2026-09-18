# The listing's icons: one body per MACHINE, not one per entry

**OPEN. Nothing here is built.** The measurements are taken on `eb8b847`; every
existing byte figure below is read out of the tree or out of a built floppy,
and every figure for code that does not exist yet is marked ESTIMATED.

It starts from a product question - *the Disk window lists 32 entries and DOS
directories are busier than ours ever were* - and the answer turns out not to
be "raise the number". The listing's entries are 27% of what it spends. The
other 73% is a 64-byte icon slot per entry, and **most of those slots hold
either a copy of one built-in body or 64 zero bytes.**

---

## 1. What a listing costs today

`kern_big`, a floppy, in `.lowbss`:

| | bytes | |
|---|---|---|
| `disk_dir` - 32 entries x `DSK_DE_STRIDE` 24 | 768 | 27% |
| `disk_icons` - 32 slots x `DSK_ICO_SIZE` 64 | **2,048** | **73%** |
| | **2,816** | |

`kern_small` already spends 1,824 for the same listing (SPEC.md 25.8: a 16-body
POOL and a one-byte index per entry). A driver-backed volume gets
`DSK_VENT` = 64 entries out of the 6KB its driver funds - 5,632 bytes of it.
And the whole shape is MIRRORED per open Disk window in a heap claim
(`VIEW_KB` = 3 on `kern_big`, 2 on `kern_small`, up to `VIEW_SLOTS` = 4).

`.lowbss` is the tightest rung in the kernel: **9,182 bytes, 34 left, 93% of the
current rung accrued** - and it sits BELOW `HEAP_SEG` in the ladder, so every
byte spent there comes off the heap and therefore off the DOS arena, byte for
byte. That is not an abstract cost this month:
docs/reports/DOS-GAMES-2026-09-16.md has Chip 'n Dale losing by **three
kilobytes**.

## 2. The entry is not the problem, and its 24 bytes are nearly all name

SPEC.md 19.1's meaningful prefix is name 0..15, type 16, handle 18..19, size
dword 20..23. The name is a NUL-terminated 8.3, so **13 of its 16 bytes are
used** and byte 17 is spare: 4 bytes an entry of padding, 128 of a 32-entry
listing. Worth taking eventually, worth nothing on its own.

## 3. THREE populations of icon, with three different properties

Read `disk.inc`'s harvest (step 4 of `disk_mount`, SPEC.md 18.3) and the slots
fill three ways:

| entry | what goes in its 64-byte slot | where the bytes really live |
|---|---|---|
| folder (type >= 2) | a byte-for-byte **copy** of one built-in body | already in the kernel image, `disk.inc:903` |
| document (type 0) | **64 zero bytes** - the generic-icon sentinel | nowhere; the appearance is composed at draw time |
| package (type 1) | its own icon, harvested from its `.o88` header | the file itself, and `ASSOC.DAT` |

**A document's icon was never in this array.** `assoc_compose` draws a page
frame and ORs an **8x8 glyph** into the interior (SPEC.md 54.3), and that glyph
lives in `assoc_glyph` - `ASSOC_NAPP` = 12 slots x 8 bytes = **96 bytes**,
resident, of which the first five are BAKED INTO THE KERNEL at build time by
`tools/os88mini.py`.

So only the third row needs storage that varies, and **in a DOS directory there
is no third row at all.** Measured on the game disks built for
docs/reports/DOS-GAMES-2026-09-16.md: LEMMINGS 68 entries, F15 66, TD1 41,
CHIPDALE 22 - **zero packages in every one.** Today each of those gets 64 slots
of zeros: 2,048 bytes of RAM spelling *no icon* thirty-two times.

## 4. The identity: `(stem, size)`, and it is already on the disk

A single machine-wide body store needs to answer *is the icon from volume A the
same as the one from volume B* - because SPEC.md 24.3 ships the core packages on
the system disk AND the apps disk, and a hard disk makes a third copy.

**`asc_lookup_x` already answers exactly that question, and has since SPEC.md
54.7.** It keys on the **8-byte name stem and the size word**, both read
straight out of the staged directory entry:

```asm
    mov dx, [si+20]             ; the size the directory reports
    ...
    repe cmpsb                  ; 8 bytes of stem
    cmp dx, [es:di+8]           ; the stem matches: the size must too
```

Measured against every package copy on the six shipped 360KB disks:

```
package copies:                                     53
DISTINCT (8-char stem, size) keys:                  29
copies the key correctly collapses:                 24

   BROWSER   13155 bytes  on os8088-360, apps360, network360
   PAINT     22007 bytes  on os8088-360, apps360, office360
   MINES      1818 bytes  on os8088-360, apps360, games360
   TELNET     12726 bytes  on os8088-360, apps360, network360
   ... 16 more
```

**29 keys for 29 genuinely distinct packages - no false merge in 53 copies**,
and 45% of what we ship is a duplicate the key collapses. 29 also happens to sit
inside `ASSOC.DAT`'s own 32 rows, which is presumably why 32 was chosen.

### 4.1 Why not a content hash, and why not an id in the header

**A hash costs the read it is trying to save.** The value of the `ASSOC.DAT`
path is that a hit costs NO sector read - `asc_lookup_x` answers before
`.h_read` ever runs. An identifier inside the icon body, or anywhere in the
32-byte header, is inside the FILE, so fetching it IS the read. `(stem, size)`
is the only identity available from the directory entry alone.

**And the header has no room.** SPEC.md 20.2 fills all 32 bytes: magic 0..1,
version 2, flags 3, link base 4..5, entry 6..7, image 8..9, bss 10..11, the
three-byte dispatcher 12..14, stack class 15, name 16..31. An embedded id is a
v3 -> v4 bump and a rebuild of every package, to buy immunity to two cases: two
different packages sharing a stem AND a byte size (did not occur in 53 copies,
and the failure is a wrong icon, which is cosmetic), and the same package at two
sizes - a `PKGZ=` build, or a hard disk carrying an older release - whose
failure is a cache MISS, not a wrong icon. **Not worth a format break.**

## 5. `ASSOC.DAT` IS the cache, already, on disk

```
ASC_ROW  equ 80    ; stem 8 + size 2 + cluster 2 + 4 reserved + a 64-byte icon
ASC_KB   equ 3     ; 32*80 + 16 + 24*4 = 2,672 bytes
```

The row carries **the whole body**. So the thing this plan proposes in RAM is a
read-through cache over a format that already exists, with the identity already
defined and already persisted - and it should BE the 80-byte row, so that
filling the cache is a read and not a conversion.

## 6. What may be purged, and what may not

A machine-wide store is a candidate for the heap rather than `.lowbss`, which
would cost **zero resident bytes** and leave the DOS arena alone. The question
is what a purge loses, and the two halves answer differently:

| | size | refills itself? | purgeable |
|---|---|---|---|
| the 64-byte bodies | 2,048 | **yes** - one read of the volume's `ASSOC.DAT`, ~2.7KB, one or two coalesced `int 13h` for every icon on the disk | **yes** |
| `assoc_glyph`, the 8x8 document glyphs | **96** | **no** - a LEARNED one needs a volume that may no longer be in the drive | **never** |

The glyphs are 96 resident bytes with initialisers today, so honouring that
costs nothing: they were never a candidate for the heap. And the case that looks
worst - *double-click a `.EXE` on a DOS-only floppy and `.EXE` loses its icon* -
**cannot happen**, because `.COM`, `.EXE` and `.LNK` resolve to app slot 4 of
`assoc_stem`, which is a built-in for exactly this reason:

```asm
    db 'DOS     '     ; SPEC.md 96: the handler for .COM, .EXE and .LNK (96.21),
                      ; and it has to be a BUILT-IN rather than a
                      ; declaration on the disk - see assoc_ext
```

The cold path - no `ASSOC.DAT` on the volume - is a per-file harvest at ~400 ms
a package, bounded by the packages in ONE directory. Measured worst case across
every shipped floppy: **15** (`apps.img` APPS, 17 entries). Every shipped disk
is written with a warm `ASSOC.DAT`, so that path is for foreign media, which by
definition has no packages on it.

## 7. SPEC.md 25.8.5.1 said no to this, and what overturns it

`kernel.asm:2224` carries the standing refusal, and its arithmetic is RIGHT:

> kern_big keeps one body per ENTRY and so cannot take the line above: its pool
> would need `DSK_NENT` bodies not to show fewer icons than it does today, and
> 768 + 32 + 2,048 is 32 bytes MORE than the 2,816 it spends now

A 32-body pool plus a 32-byte index is indeed 32 bytes worse than 32 bodies.
**What that argument is missing is how many bodies a directory actually needs**,
and the answer is 15 in the worst shipped case and 0 in every DOS one. The
premise *the pool must equal `DSK_NENT` or an icon degrades* is what the
measurement retires - and SPEC.md 25's generic-icon fallback is a picture the
system already has, which `kern_small` has shipped against for a cycle.

## 8. The arithmetic

Per listing, `kern_big`, entries at 24 bytes plus ONE reference byte each,
with the bodies no longer per listing at all:

| entries | `disk_dir` | index | per-listing total | vs today's 2,816 |
|---|---|---|---|---|
| 32 | 768 | 32 | **800** | **-2,016** |
| 64 | 1,536 | 64 | **1,600** | **-1,216** |
| 128 | 3,072 | 128 | **3,200** | +384 |

...plus ONE machine-wide body store, whichever entry count is chosen:

| store | bytes | where |
|---|---|---|
| 16 `ASC_ROW`s | 1,280 | heap claim, purgeable |
| 32 `ASC_ROW`s | 2,560 | heap claim, purgeable - holds all 29 distinct icons we ship |

**A 64-entry listing with a purgeable 32-row store is 1,600 resident bytes
against today's 2,816** - double the listing, **1,216 bytes given back to the
arena**, and the store costs nothing resident. A 128-entry listing - which
covers LEMMINGS (68), F15 (66) and TD1 (41) outright - is +384.

And the per-window heap mirror loses its icon half entirely: `VIEW_KB` 3 -> 2 at
64 entries, **1,024 bytes of heap per open Disk window**, four of them.

## 9. The one constraint, and it DISSOLVES rather than being lifted

`FS_IOFH` (files.inc:247) holds the icon base's HIGH BYTE in one byte, which is
why `files.inc:694` requires `nmax * DSK_DE_STRIDE` to be a multiple of 256 and
why the stride cannot fall below 24 at 32 entries.

**The first draft of this plan made widening it to a word wave 1. That was
wrong and is recorded here so nobody builds it.** `FS_IOFH`'s only job is
*where the icon slots start inside THIS WINDOW'S OWN cache* - six sites, and
`fmv_viofs` is in as many words "the only reader". Once the bodies are in a
machine-wide store, a window's cache holds entries and reference bytes and
**has no icon region at all**, so the field has no job, the `%if` that guards
it has nothing to guard, and both are deleted by wave 2 rather than widened
ahead of it. Nothing between here and there needs the stride to move: the base
is `32 * 24` = 768 throughout, which is a multiple of 256 already.

The same is true one level along of `dsk_ioff`'s driver-backed base
(`disk.inc:2419`, `DSK_VENT * DSK_DE_STRIDE`) - it is the same fact about the
same vanishing region, and it goes at the same time.

## 10. The waves

1. **The reference byte.** `dsk_ico_ofs` resolves an entry to a body through a
   one-byte reference instead of `index * 64`; folder and generic become
   sentinel values that name a built-in rather than a copy. **Every reader
   already goes through `dsk_get_icon_x`**, which stages into `dsk_ico` and
   hands back SI - so no caller changes. `kern_small` has this shape already
   (`dsk_iconext`, `dsk_icofld`); this generalises it to both kernels and
   deletes its `%ifdef`s. The bodies are still per listing at the end of this
   wave, which is what keeps it independently buildable.
2. **The machine-wide store**, as `ASC_ROW`s, keyed `(stem, size)`. The
   per-listing and per-window icon regions go here, and `FS_IOFH`,
   `fmv_viofs`, `dsk_ioff` and the multiple-of-256 `%if` go with them.

   **IT IS A NEW CLAIM AND NOT THE `ASSOC.DAT` BUFFER**, which is what the two
   questions below settled. `asc_seg` is read from the file with one
   `dsk_read_chain_x` straight into the claim at offset 0 and is WIPED on a
   volume switch, so making it multi-volume would mean staging the file
   somewhere else to merge from - a second buffer either way. A separate
   ADDITIVE store leaves `asc_use_x` untouched and takes its rows from
   whatever answered: an `asc_lookup_x` hit, a harvest, or a compose.

   - **There is no writer to break.** The kernel never writes `ASSOC.DAT` -
     `asc_s_name` has two occurrences, its definition and the directory walk
     that FINDS the file, and `tools/os88disk.py` writes it warm at image-build
     time. Merging volumes in RAM cannot corrupt a file nothing writes.
   - **32 rows is not enough, and the file's cap is not the store's.** Distinct
     package icons a machine can demand at once, measured over the shipped
     images: 9 with the system disk alone, **26** with system + apps, **29**
     with everything mounted - plus up to `ASSOC_NAPP` = 12 composed document
     bodies, one per app slot. `ASC_NAPP` is 32 and its own comment reads
     *"was 16, and the shipped apps disk holds 15 - one package of headroom,
     with no guard"*. The FILE stays at 32 rows, which is ample for one volume
     (the busiest holds 15); the STORE is sized for the machine at **48**.
3. **Purgeable.** `MEM_P_ICO` at `MEM_PG_TRIV`, the cheapest rank there is:
   losing it costs a REDRAW, not a read, because the next mount fills the rows
   back from the volume's own `ASSOC.DAT` in one file read. The 96-byte
   `assoc_glyph` table is NOT in it. **BUILT.**

   **One thing in it is reasoned and not gated**, which `tests/icostore.py`
   says in its own docstring: `ico_body` refuses a row past `[ico_n]`, so a
   listing staged before a shed cannot resolve a row that no longer exists.
   The row does not cover it - navigating after a shed re-mounts, so every
   reference read is fresh, and removing the guard leaves the row GREEN
   (checked, not assumed). Covering it wants a repaint without a mount and a
   PIXEL test, the failure being a wrong picture rather than a wrong number.
4. **Raise `DSK_NENT`.** The number is a product decision once it is nearly
   free; 64 is a net saving, 128 costs 384 bytes.

## 11. What the gates must say

- `tests/unit/t_lowwin.py` - the `.lowbss` window's ORDER, already exists.
- `kernel.asm`'s `SKB_DSK` assertion - recut per arm; it is written in terms of
  `DSK_NENT`, `DSK_DE_STRIDE` and `DSK_ICO_SIZE` and all three move.
- `files.inc:338`'s `VIEW_KB` assertion - recut.
- **A NEW ROW, and it is the one that matters**: two volumes mounted in turn,
  each carrying a copy of the same package, and the body stored ONCE - read off
  the guest, not off the screen. Break it by keying on the stem alone and watch
  two different packages collide; break it by keying on the size alone and watch
  the same package store twice.
- A row that lists a 68-entry DOS directory and asserts the store is EMPTY.
- `soak -k 'disp*'` for the drawing, which is the half a byte count cannot see.

## 11.1 THE LAST DUPLICATE: `ASSOC.DAT`'s buffer — BUILT, and not the way this section costed it

`asc_seg` was a 3KB claim holding up to 32 rows of `stem 8 + size 2 +
cluster 2 + 4 reserved + a 64-byte BODY`. Once the store held the bodies,
**2,560 of those 3,072 bytes were a second copy in RAM** — the thing this
whole plan is about.

**What this section proposed** was a migration that keeps the claim: re-key
the store to the stem, absorb the bodies, **compact the rows in place to 16
bytes**, and shrink the claim to ~1KB, which needed `mem_regrow` to take a
claim DOWN. **What was built is simpler and gives back three times as much**:
the claim is a **FILE BUFFER**. `asc_use` reads `ASSOC.DAT` into it, takes the
declarations (`asc_merge_ext`), the locations and glyphs (`asc_seed`) and now
the bodies (`asc_absorb`), and then **frees it** (`asc_drop`) before it
returns. SPEC.md 54.7.4 is the contract.

The cheap-version warning above still stands and is *why* this shape works.
Freeing the claim per MOUNT would put ~2 `int 13h` back on every folder
navigation, because `asc_vol` is what makes a re-entry a compare instead of a
re-read. Freeing it per **volume switch**, after everything in it has been
taken, costs nothing at all: the stamp still works, and there is simply no
buffer left to re-read from.

**Five things it turned out to need, and three of them were not in the plan.**

1. **`ico_key_stem`** composes the store's twelve-byte key from a row's stem
   (`<STEM>.O88` NUL-padded) rather than re-keying the store, so an
   **absorbed** body and a **harvested** one land on ONE row. A second key
   shape would have been the same duplication with the numbers rearranged.
2. **The store's claim is made BEFORE the absorb walk.** `mem_claim` can
   compact, the cache's claim is movable, and the walk holds a row offset
   against a segment a compaction moves. `ES` is reloaded per row anyway.
3. **`asc_lookup` is gone and its question went UNGATED.** It searched the
   claim for an offset; with no claim, *is this body already in RAM* is the
   store's question and `ico_have` asks it on **both** kernels — which is a
   saving the plan never costed, because `kern_small` has no `ASSOC.DAT`
   (SPEC.md 54.0) and its harvest was therefore reading a package's first
   sector every mount for a body the store had held since the last one. On
   the 4.77 MHz machine that is ~400 ms apiece.
4. **`asc_vol` needed a second half to its compare.** The stamp means "this
   volume's bodies are in the store" now, and the store is PURGEABLE, so a
   shed leaves it vouching for bodies that are gone. `cmp byte [ico_n], 0`
   beside it, five bytes, in the stamp's only reader. **Clearing the stamp
   from `ico_need` was built first and is worse by one mount** — `ico_need`
   runs on the first body the harvest wants, which is *after* `asc_use` has
   already declined to re-read.
5. **Eager absorption had to be sized rather than assumed.** A buffer that
   may still be wanted cannot be freed, so there is no lazy version. Measured
   across every shipped volume: the busiest declares **25 rows of which 24
   carry a body**, and the union is **28 distinct `(stem, size)` pairs**
   against `ICO_NROW`'s 48, with `ASSOC_NAPP` capping composed document
   bodies at another 12.

**And the merge with SPEC.md 54.3.2's glyph column added a sixth thing that
was not optional.** That work gave a version 2 `ASSOC.DAT` row an 8-byte
column holding the glyph a package SHIPS, because the reduction of a 16×16
icon is not always a usable 8×8 one — DOS's CRT reduces to an empty block — and
a cache HIT read that column off the row. A hit has no row once the claim is a
file buffer, and a store carrying only the body answers with the *reduction*:
it does not fail to write the glyph, it **downgrades one `asc_seed` had
already resolved**, so the picture gets quietly worse the first time a folder
is browsed. `tests/dosglyph.py` leg 4 caught it, and it is worth saying that
the first fix considered — *leave a resolved glyph alone* — would ALSO have
failed, because that row poisons the slot with the reduction rather than with
zeros precisely so a writer that skips blanks cannot pass. So the store row
carries the glyph: `ICO_ROW` 80 → 88 on `kern_big` only, `asc_absorb` puts a
v2 row's in beside the body, and `asc_take` prefers it. 384 bytes of a claim
whose sibling just returned 3,072.

**What it cost and what it bought**, measured: `.text` +0, `.bss` +0,
**`.cold` +115 on `kern_big` and +38 on `kern_small`**, no rung crossed on
either and both footprints byte-identical — against a **3,072-byte heap claim
that is no longer held at all**, and which `docs/HEAP-CLAIMS.md` had measured
acting as a mid-arena BARRIER (taken on a volume switch, so on a machine that
has been used it sat wherever the arena had room, holding 40KB out of reach on
the run that found it). It is still claimed inside `asc_use` and still
declared movable there, because `dsk_read_chain` reaches the sector cache's own
claims and those can compact (SPEC.md 18.95) — but now that is a
within-call requirement rather than a session-long one.

**And the saving has a number on the glass.** `tests/ascabsorb.py` mounts B:,
finds **no `MEM_K_ASC` record in `mem_tab`** and **23 rows in the store** —
the whole volume's packages, absorbed at the ROOT mount, before anything has
listed the folder they live in. Entering `B:/APPS` then lists 13 iconned
entries and adds **zero** rows, for **4 reads of 18 sectors** in total.

## 12. What would kill it, and what is still open

- **The per-window mirror CAN drop its bodies - settled, and it is the reason
  the whole thing works.** `fmv_copy_in` already falls back to the global
  snapshot when a window's own claim was refused, so "paint from somewhere
  else" is a path that exists. What stops a BACKGROUND window using it today is
  that the global snapshot is the CURRENT mount: a window listing volume B
  cannot read bodies staged for volume A, and there has never been a
  volume-independent way to name one. **`(stem, size)` is exactly that name**,
  so a background window holding entries and reference bytes resolves every one
  of them against the shared store whatever is mounted now - which is strictly
  more than it can do today.
  The residual risk is EVICTION, not reach: a reference whose row has been
  purged resolves to the generic icon, which is SPEC.md 25's existing fallback
  and not a crash, and the row returns at the next mount. Sizing is what keeps
  it rare, and 29 distinct icons across the entire shipped set fit in 32 rows
  with three spare.
- **The hard disk's 64 entries come out of the DRIVER's 6KB claim**, so shrinking
  the listing there changes `HDD_LISTKB` and rebuilds a `.DRV`. Wave 5 only.
- **`ASSOC_NAPP` is 12 with five built-ins**, so a machine can know **seven
  learned document appearances at once**. THIS CONSTRAINT IS NOT NEW and it is
  not this work's - it is recorded here because the same measurement pass is
  what surfaced it, and because it may bind before the entry count does on a
  hard disk full of applications. **It is a FOLLOW-ON, to be researched once
  the icon work is done**: raise it to ~24, and the questions are what that
  costs `assoc_stem` (8 bytes a slot), `assoc_glyph` (8 more), `assoc_drv` and
  `assoc_clus`, whether `ASC_KB`'s 3KB still holds the file it implies, and
  what a machine with a hard disk full of applications actually needs. Nothing
  in waves 1-5 depends on the answer and nothing in it should be taken before
  they land.

## 13. WHAT THE STORE COST THE FLOOR MACHINE, measured both sides

`tests/regrowshed.py` went red at wave 2 and stayed red through waves 3-5, and
it is the only row in the tree that did. It is recorded here in full because
the answer is **not a defect** and the three experiments that did not find it
are worth more than the one that did.

**What it is not.** Three things were tried on the theory that the store's
2,048-byte claim was too big or in the wrong place, and all three left the row
red: halving `ICO_NROW` on `kern_small`; making the claim purgeable (wave 4,
which shipped anyway on its own merits); and moving the store OFF the heap
entirely into `.lowbss`. That last one is the instructive failure - it is the
control that looks decisive and is not, because `.lowbss` sits below
`HEAP_SEG`, so a 1,920-byte array there took `KERN_SIZE` **+1,536** and the
heap DOWN by the same, and the experiment made the pressure it was testing
worse. A control that changes two quantities answers about neither.

**What it is.** Two heap maps, the same script, the same machine
(`os8088_5150_cga_128k`, 128KB, 52.5KB of heap, `HEAP_SEG` 0x12e0, top
0x2000), taken at `0286f13b` (wave 1) and at the store's landing. One Note Pad
open, the manual loaded, at the moment the row asks for a second instance:

```
 wave 1                              the store
 ---------------------------------   ---------------------------------
 VIEW    12e0   2,048  PURGE         VIEW    12e0   2,048  PURGE
 DIRW    1360   9,216  PURGE         DIRW    1360   9,216  PURGE
 FREE    15a0   1,024                FREE    15a0   2,048   <- ICO, shed
 WSAVE   15e0   6,144  PURGE         doc1    1620  16,384
 doc1    1760  16,384                FREE    1a20   9,728
 FREE    1b60   4,608                region1 1c80  14,336
 region1 1c80  14,336
```

The quantity that decides a launch is neither `free` nor `in caches` but the
largest run a top-down region claim can reach **after every cache has been
shed** (SPEC.md 50.3.2 - a region's base is its CS, so it is claimed
`mem_claim_hi`). Wave 1: 12e0..1760 = **18,944**. The store: 12e0..1620 =
**13,312**, against the 14,336 a Note Pad instance wants. **Short by 1,024
bytes**, and short at both ends - the top run is 9,728 - so the machine is
honestly full and `LD_ENOMEM` is the right answer.

**Why the maps differ is one branch in `mem_regrow`, and both arms are
correct.** The manual's claim grows 1,024 -> 16,384. At wave 1 that took path
3 (`.move` -> `mem_hifit`, the highest run that will take it) and landed
HIGH at 0x1760, leaving the low arena in one piece. With the store in, it
takes path 2 - an extend in PLACE after `.shed` - and lands LOW at 0x1620,
because `MEM_P_ICO` is `MEM_PG_TRIV` and sits directly BELOW the claim, so
shedding it is exactly what makes the in-place extension fit. `mem_regrow`'s
`.shed` returns to `.grow` and not to `.move` deliberately ("an extend in
place leaves no hole behind it"), and that is the better rule in general: it
is *this* heap, at *these* four addresses, where it costs a kilobyte.

So the placement is arithmetic, the shortfall is 1,024 bytes, and the row's
fourth leg had been riding on it. It is re-derived rather than argued with:
it empties the first note (File > New, `np_resize(NP_KB0)`, a shrink that
cannot fail) before launching the second, which funds the instance out of
15,360 returned bytes and leaves the refusal where the row wants it - in
`np_load`'s grow, against an ~11KB run, with five kilobytes of margin instead
of one.

**And the re-derivation found a second thing, which is the one to remember.**
The row printed a NOTE whenever the largest free run it had measured was
already 16KB, saying the shed had not been exercised and the row wanted
re-deriving. That note was WRONG, and it had been firing at wave 1 too. It
reads the run BEFORE `File > Open`, and on `kern_small` the dialog is
`FDLG.DRV` (SPEC.md 38.0): `mod_need` claims its image out of that very run
and holds it for the whole of `np_load`, so what `mem_regrow` faces is always
smaller than what the row measured. Taking `call mem_shed_one` out of
`mem_regrow.shed` fails the load against a measured **18,944-byte** run. The
leg is asserted on the CACHES FALLING now (19,456 -> 11,264), which is the
fact that can say it.

**The cost, stated plainly.** On `kern_small` the store did not remove a
duplicate - wave 1's `disk_icons` was already one pool for the machine - it
moved 1,216 resident `.lowbss` bytes into a 2,048-byte purgeable heap claim.
That is the right trade by this project's own rules (a `.lowbss` byte is
resident for ever and comes off the DOS arena too; a purgeable byte comes back
when the last listing closes), and it is worth writing down that the trade is
what it is rather than a saving: **-1,216 for ever, +2,048 while a listing is
warm.** What `kern_small` gets for it is the 64-entry listing and the shed.

## 14. THE SIZE PASS, and what it found

The store shipped in five waves and was never size-passed. It has been now,
and the finding is worth more than the bytes: **almost none of it was tight
code written large. It was capabilities the kernel already owned, written
again** — which is what happens when a feature is built in waves, each wave
correct on its own and none of them looking sideways.

**Measured**, `nasm -DKERNSIZE` both arms, at the branch's own baseline and
after:

| | `.text` | `.cold` | `.bss` | `.lowbss` | `KERN_SIZE` |
|---|---:|---:|---:|---:|---:|
| `kern_big` before | 50,023 | 41,257 | 6,141 | 7,966 | 112,128 |
| `kern_big` after | 50,023 | **40,954** | **6,137** | 7,966 | **111,616** |
| `kern_small` before | 37,290 | 26,731 | 4,179 | 5,236 | 75,776 |
| `kern_small` after | 37,290 | **26,596** | 4,179 | 5,236 | **75,264** |

**-303 and -135 of code**, `.lowbss` unmoved on both — the -1,216 of
`.lowbss` this whole plan is about is untouched — and **BOTH KERNELS UNCROSSED
A 512-BYTE COLD RUNG**, kern_small's having been standing at 3 bytes of
headroom. `ICO_KB` falls 5 -> 4 with the row, so the purgeable claim returns a
further **1,024 bytes** of the arena this plan exists for.

### 14.1 The duplicates, in the order they were found

Each of these is one capability with two bodies of code, and in five of the
nine cases the second body's own comment said so.

1. **`asc_row_glyph` and `asc_take`'s glyph half are the SAME LADDER** —
   shipped glyph, else the body reduced — and they are the same code because
   they are the same layout: `ASC_ROWGLY - ASC_ROWICO` and `ICO_R_GLYPH -
   ICO_R_BODY` are both 64. `asc_gly_pair` is that ladder once and
   `asc_row_glyph` is two tail jumps into it; `disk.inc` asserts the delta at
   the point both constants exist.
2. **The all-zero test (the UNRESOLVED and ICONLESS sentinels, §54.2 and
   §54.7.3) was written THREE times**, and two of the three read the same
   sixteen words. `asc_inkck` is it once.
3. **`assoc_glyph_take`'s copy is `assoc_stage`**, §20.2's staging idiom,
   ES:SI -> DS:DI, already in the same file.
4. **`ico_key_of`'s and `ico_key_doc`'s byte loops are `dsk_ncopy`**, §4's
   counted copy — which preserves SI as well as DI, so the key builder banks
   SI not at all where the hand-rolled loop had to.
5. **`ico_key_doc` re-derived where a slot's glyph lives**, shift and all,
   beside `assoc_glyph_di`.
6. **`ico_glyph_put` re-derived the claim, the stale-row guard and the row
   offset** beside `ico_body`.
7. **`ico_add` re-derived everything `ico_find` had just computed** — a second
   `ico_need`, a second read of `[ico_n]` and an `ico_rowoff` over it — when
   the scan that had missed ended standing on the free row.
8. **The harvest and the redirected-volume pass classify an entry
   identically**, differing only in what a MISS means, which is what a carry
   says: `ico_ref_try`.
9. **"Stage the body a reference names into `dsk_ico`" was written in two
   files**, and `dsk_ico_ref_at` was a routine, a frame and a carry handing an
   address to its ONE caller six instructions along.

One cut is not in the icon store at all and is here because the same sweep
found it: `ld_pkg_byname` (§21.5.3) had written out `assoc_stem_of`'s
pad-and-stop walk a second time.

And two more came out of a second look, after the first pass had refused both:

10. **`asc_seed` and `asc_absorb` walked the same table twice**, back to back
    out of `asc_use`, at the same stride from the same base, each with its own
    frame and row pointer. One walk now; `asc_row_take` is the absorb's body.
    What makes it safe is not the ordering (neither half reads what the other
    writes) but `ico_need` staying IN FRONT of the walk: `mem_claim` compacts
    on its refusal path and the loop holds a row offset against the segment a
    compaction moves. The merge was BUILT WITHOUT THAT and it is the one thing
    in this pass that would have shipped a live defect - the guard came back
    on re-reading 11.1 point 2 of this file, at a cost of 15 of the 32 bytes
    the merge saved.
11. **The store's key IS `ASSOC.DAT`'s own stem** on kern_big - see 14.2's
    first row, which is a refusal reversed.

### 14.1.1 The refusal that was reversed: the key IS the stem

The first pass refused re-cutting the store's key to `(8-byte space-padded
stem, size)` on the grounds that `assoc_stem_of` is inside `%ifdef
OS88_ASSOC`, so `kern_small` would have to grow its own stem walk. **That was
a cost priced at zero rather than measured.** Measured, by assembling both
bodies:

| | today | stem key |
|---|---:|---:|
| `ico_key_of`, `kern_small` (its own walk) | 22 | **45** |
| `ico_key_of`, `kern_big` (`assoc_stem_of`) | 22 | 29 |
| `ico_key_stem` | 56 | **20** |
| `ico_key_doc` | 43 | 35 |
| `kern_big` total | 121 | **84** |

So it is **-37 on `kern_big` and +23 on `kern_small`** — and the key shape is
INTERNAL TO ONE BUILD. Nothing on disk names it, no ABI names it, and
`kern_small` has no `ico_key_stem` to agree with in the first place. So it is
taken on `kern_big` and not on `kern_small`, which is CLAUDE.md's own
`gfx_points` rule one feature along: *the answer may differ per build*.

**It loses no identity at all**, which is the part worth keeping: a key is
built for a type-1 entry and for nothing else, and every package is a `.O88`
(SPEC.md 20.1) — so `<stem>` and `<stem>.O88` name the same set and the four
extension bytes were carrying no information. What it buys beyond the bytes is
that the two builders now AGREE BY CONSTRUCTION: both copy the same eight
bytes, where one used to rebuild what the other would have composed.

And `ICO_ROW` 86 -> 82 takes `ICO_KB` from 5 to 4 — **1,024 bytes off a
purgeable claim that stands in front of a DOS program**, which is a bigger
number than the code.

### 14.2 What was costed and REFUSED

- **The glyph column replaced by a per-slot "this glyph is SHIPPED" bit.**
  384 heap bytes and ~96 of code, which is the largest single removal the
  store offers. **`tests/dosglyph.py` step 4 refuses it**: it poisons the slot
  with the reduction and then opens `APPS/`, and the repair has to come from
  somewhere. A flag can say *do not downgrade this slot*; it cannot RESTORE
  eight bytes, and the only other place those eight bytes live is the
  package's own first sector — the ~400 ms read the hit exists to avoid. The
  column is load-bearing and stays.
- **A 2-byte checksum key in place of the 14-byte one.** Priced at **-9 code
  bytes** (`ico_scan` -9, `ico_add` -3, the builders +3) plus 12 bytes a row
  of a claim that is purgeable anyway — against a new failure mode, a
  collision drawing the wrong icon. Refused on the ratio, not on the risk.
- **`ASSOC.DAT` carrying the 12-byte 8.3 name instead of the 8-byte stem**,
  which deletes `ico_key_stem`'s composition outright (~-45 plus ~-16 from
  retiring `[asc_rowsz]`'s two widths with it). **The tree has already taken
  this decision and it went the other way** — `ASC_ROW1`'s own comment says
  version 1 is *"READ, because an installed volume's cache was written once at
  install and a refusal would send that machine back to the pre-54.7 harvest
  in silence; never written"*. The name cannot share a width with the stem (it
  needs the size and cluster words moved), so a v3 row means the kernel either
  understands both shapes — which costs more than it saves — or refuses every
  `ASSOC.DAT` already in the field. The version byte makes that refusal SAFE
  (it is the existing *"a cache this build does not understand"* path, so no
  corruption and no wrong picture) but not FREE: an upgraded machine loses its
  declarations until each program's folder has been browsed, which is the
  sentence above, verbatim, about the version that was kept for exactly this
  reason.
- **The mount path behind `mod.inc`.** It is the largest lever there is and it
  is illegal: `mod_need` goes to `[dsk_bootvol]` and only there, so a data
  floppy mounted on a one-drive machine would want the system disk back to
  draw its icons.
- **The store claimed at mount time instead of lazily**, which shrinks
  `ico_need` from 54 bytes to ~14. It is the plan's own design (§10 wave 2: a
  machine that never opens a Disk window never claims it) and a regression on
  the 128KB machine.

### 14.3 The accounting, since the brief's was wrong in two places

The pass was briefed at "≈+1,048 code bytes, halve it". Two of the four
modules in that figure are not this feature:

| named | bytes | whose |
|---|---:|---|
| `kernel/loader.inc` `ld_pkg_byname` / `ld_pkg_upc` | +159 | §21.5.3, the DOS shell's `open <document>` arm |
| `kernel/disk.inc` `dsk_rah_drop` / `osapi_dsk_cache_x` / `dsk_rah_cap` | +89 | §18.95.8, the read-ahead cache's width command |

and the brief's list of "the drawing side" — `ico_core`, `ico_pass`,
`ico_pool`, `ico_disk32`, `ico_app16` and the rest — is `kernel/icons.inc`
(§10), which predates this branch entirely and shares nothing with the store
but the `ico_` prefix. What the feature itself costs, and what it costs now:

| module | before | after |
|---|---:|---:|
| `kernel/disk.inc`, the store | 573 `.cold` + 14 `.text` + 14 `.bss` | **431 + 14 + 10** |
| `kernel/assoc.inc`, the glyph column and the absorb | 338 `.cold` | **263** |
| `kernel/memory.inc`, `mem_pg_own`'s row | 6 `.text` | **6** |
| `kernel/files.inc`, `fmv_get_icon` | 18 `.cold` | **-1** |

...against **806** of feature, which is 1,048 less §21.5.3's 159 and
§18.95.8's 89. **-303 of 806 on `kern_big`, 38%**, of which 22 is
`ld_pkg_byname`'s and so not the store's either.

The `memory.inc` row is `dw MEM_P_ICO, ico_seg, MEM_P_ICO_N` and is what makes
the store purgeable at all — three words, nothing to cut.
