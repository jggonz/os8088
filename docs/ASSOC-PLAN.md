# File type associations — investigation and plan

**Status: COMPLETE. Phases 1-5 built, measured and verified on VGA, CGA and
Hercules; `tests/assoctest` passes 6/6; the icon style is settled (the inset
page - §9.1); the on-disk cache is built (SPEC.md §54.7).**

The ask: a file with a known extension should show the *associated program's*
icon, marked so it reads as a document rather than as the program; and
double-clicking it should open it in that program. Build-time associations to
start; a running program must be able to register a new one or take over an
existing one. It must be small, and it must not cost kernel RAM once booted.

This document is the investigation, the design decisions with the alternatives
I rejected and why, a measured budget, and a phased plan.

---

## 1. What the tree already does, and what that buys

Four findings decided the design. Three of them are the reason this can be
cheap; the fourth is the reason it cannot be free.

### 1.1 The per-file icon is already paid for

`disk_icons` is `dsk_nmax` × `DSK_ICO_SIZE` (32 × 64 in `.lowbss`; 64 × 64 out
of the donated claim on a driver-backed volume, `disk.inc:40`), and it is
**fully rewritten every mount** (SPEC.md §18.3 step 4). A **type-0 file's slot
is currently all zero** — the generic-icon sentinel — and every viewer already
knows what to do with a slot that is not: `fm_draw_icon16` ORs the 32 words,
draws the body if anything is set, and falls back to `ico_app16` if not
(`files.inc:3458`).

So a document icon composed into that already-allocated slot at harvest time
costs **zero bytes of new per-file RAM and zero work in any paint**. Both Disk
window views (list and icon grid — `files.inc:3687` and `:3762`) get it with no
change to a single line of drawing code, the per-window view caches
(SPEC.md §22.1) copy it wholesale as they already do, and so does anything
built on the mount snapshot later. That last part is the point: the user asked
for "any future place that does file things", and putting the answer in the
snapshot rather than in a drawing path is what makes that true by construction
instead of by discipline.

**This is the load-bearing decision: compose at mount, in `disk.inc`'s harvest,
never at draw time.** PERFORMANCE.md's standing budget is untouched — no redraw
path gains an instruction, and no repaint gains a disk read.

The trap it brings: **harvest order**. The doc icon needs the app's glyph, and
the app may sit in the same directory *after* the document in the name sort
(`BEACH.BMP` before `PAINT.O88`). So the harvest becomes two passes over the
≤64 entries — pass A exactly as today (type-1 icons and folders), plus filling
any association row whose app is in this directory; pass B composing type-0
document icons. Both are loops over data already in hand; neither adds I/O.

### 1.2 The table need not touch `.bss` at all

`-f bin` zeroes nothing, so initialised data in `.text` is in the image and is
writable at runtime. The tree already leans on this deliberately — `ui_tm_cwd`
("In .text: -f bin zeroes no .bss"), `[dsk_fatw0]`/`[dsk_fatd0]`, and
`[fdlg_win]`, which is in `.text` precisely so `fdlg_grab` can read it on the
machine's first mouse press.

So the association table is **N rows of `.text` carrying the build-time
defaults**, and a runtime registration writes over a row. One copy of the data,
no `.bss`, no boot-time init code, no `.ovl` staging. Build-time and runtime
associations are the same bytes, which is also why "take over an existing one"
needs no special case.

### 1.3 The open path already has the branch

`fm_open_sel` (`files.inc:1863`) is the single open path — double-click, Enter
and File > Open all reach it — and it branches on the §19 type word: ≥2
navigates, anything else posts index+1 to `[ld_pending]` with the poster's state
block in `[ld_pwin]`, and `ui.inc` runs the loader once the lock drops. A type-0
file goes to the loader and comes back **"Bad package"**, which SPEC.md §19.1
calls "the truthful verdict for double-clicking a data file".

That is exactly the branch an association intercepts, and nothing else moves. A
type-0 file with **no** association keeps that message, byte for byte.

`ui_tm_open` (`ui.inc:973`) is the working template for the rest of it: bank the
volume with `osapi_file_here`, mount elsewhere, `dsk_find_name`, `ld_run_body`,
put the volume back, and report `LD_*` failures through `ui_note`. The
association runner is that routine with a small search in place of one fixed
mount to A:.

### 1.4 The footprint is the constraint, and it is 1,536 bytes

Measured on this tree at `a19e4a8`, by `%warning`-ing the guards:

| guard | used | limit | spare |
|---|---|---|---|
| `KERN_BUDGET` — the **footprint** | 74,752 | 76,288 | **1,536** |
| `KERN_CODE_MAX` — the **segment** (`.text`+`.bss`) | 57,361 | 65,536 | 8,175 |

The segment is not the constraint; the footprint is, by more than 5×. Every
byte of `.text`, `.bss` **and** `.cold` spends the same 1,536, so moving code
cold buys this feature nothing (CLAUDE.md's standing warning). `.ovl` is free
and useless here — the table must be resident.

One small piece of slack: `kernel.bin` is 63,944 and `KIMG_PARA` rounds the
image to 512, so the first **56 bytes** of `.text` growth are already paid for.

§8 of this document is the estimate against that 1,536.

---

## 2. Design decisions

### 2.1 The icon: a page outline with the app's glyph inset — **recommended**

A 16×16 document body built at mount from two pieces: a hand-authored
dog-eared page frame in `disk.inc`'s `.text` (the `dsk_folder_ico` precedent —
"the one icon not harvested off the disk"), with the app's glyph reduced to
8×8 and OR'd into the page's cleared white interior.

| option | RAM cached per **app** (§2.2) | verdict |
|---|---|---|
| **page frame + 8×8 inset** | **8 bytes** | recommended |
| app's full 16×16 body + corner badge | 64 bytes | **rejected on budget**: 12 apps = 768 bytes, half of everything left |
| page frame + 3 letters of the extension | 0 bytes | rejected: the ask was the program's icon |
| a diagonal or overlay across the app's icon | 64 bytes | rejected: same cost as the badge, and it destroys the glyph it is marking on a 1bpp screen |

The badge variant is the more *recognisable* of the top two and I would take it
if the budget allowed; it does not. Worth revisiting if the footprint guard is
ever raised again.

**The reduction is majority-of-2×2** — a 2×2 block lights if ≥2 of its 4 source
pixels are ink. OR-of-4 turns a typical 40%-ink Mac icon into a near-solid
blob; point-sampling drops every one-pixel stroke. Majority is the one that
keeps a silhouette at 8×8 on a 1bpp adapter. It runs **once per row**, at
resolution time, never in a paint.

Only the icon's **data** plane is reduced, not its mask: the glyph lands inside
a page interior the frame has already cleared to white, so "ink" is the whole
of what the inset means. That is what makes the cache 8 bytes and not 16.

### 2.2 Two tables, not one: extensions point at apps — **recommended**

A flat row of extension + stem + glyph is the obvious layout and it is the
wrong one, because **the glyph belongs to the app and not to the extension**.
`BMP` and `GIF` both mean Paint, and a flat table caches Paint's glyph twice.
Normalising costs one indirection and buys roughly triple the associations per
byte:

```
app slot - 16 bytes, indexed by shl 4
  +0   8   the app's 8.3 STEM, space-padded    'PAINT   '   ('.O88' implied)
  +8   8   the mini glyph, 8x8, one byte a row, bit 7 leftmost

ext slot - 4 bytes, indexed by shl 2
  +0   3   extension, uppercase, space-padded  'BMP'
  +3   1   app slot index
```

Both strides are powers of two on purpose: an 8086 has no `shl reg, imm` past
1, so a shift through CL is still far cheaper than the `mul` a 17- or 20-byte
stride would force, and the harvest does this lookup once per listed file.

**No flags byte in either.** "Slot free" is `stem[0] == 0` / `ext[0] == 0` — a
sanitized display name's bytes are 0x21..0x7E and can never be 0 (SPEC.md
§19.1) — and "glyph unresolved" is the 8 bytes ORing to zero, which is exactly
the all-zero sentinel `fm_draw_icon16` already uses on `disk_icons`. An icon
whose reduction is genuinely blank is indistinguishable from an unresolved one
and draws the bare page, which is the correct answer for it anyway.

Sizing is two constants, and the recommendation is the middle row:

| apps × exts | bytes | shipped defaults leave |
|---|---|---|
| 8 × 16 | 192 | 4 apps, 11 exts free |
| **12 × 24** | **288** | **8 apps, 19 exts free** |
| 16 × 32 | 384 | 12 apps, 27 exts free |

For comparison, the flat 20-byte row this replaces was 160 bytes for **8**
associations total. 12 × 24 is +128 bytes for 24 across 12 distinct programs.

One known limitation, not worth code in v1: if every extension pointing at an
app is taken over, that app's slot leaks. With 12 slots and no expected churn
that is acceptable; a compaction sweep on a full-table registration is ~30
bytes if it ever bites.

Rejected as the *primary* key: a **(drive, cluster) locator**. A cluster cannot
be written at build time, does not survive a disk rebuild, and does not survive
the floppy being swapped. A name survives all three. But a cluster **hint** is
what makes §2.7's search affordable and is carried alongside, in two parallel
arrays rather than in the slot, so the power-of-two stride survives:

```
assoc_clus   12 words   the directory cluster the app was last SEEN in
assoc_drv    12 bytes   ...and the volume it was seen on
assoc_dfold  12 bytes   build-time folder: 0 root, 1 APPS, 2 GAMES  (48 bytes)
```

`assoc_dfold` is what §2.7 rung 4 reads, and it needs a home of its own because
the 16-byte app slot is full — it is the one part of a build-time default that
cannot be a cluster, since no cluster is knowable when the kernel is built.

Uppercase-exact comparison on the extension, matching SPEC.md §19.1's existing
`"O88"` rule and for the same reason (foreign OSes uppercase short names on
write). The extension comes off the staged display name — one dot, by 8.3
construction, and the sanitizer leaves `.` alone since 0x2E is inside
0x21..0x7E.

Associations are consulted for **type 0 only**, so a package can never be
shadowed by an `O88` row, and folders are never associated. No new rule; it
falls out of the branch's position.

### 2.3 Delivery: the app **pulls**, the kernel does not push — **recommended**

The instance record is **full**: 32 bytes with `I_CYC` ending at 31
(`instance.inc:26-45`). A push (the kernel calling a document hook) needs a
callback pointer somewhere, and the alternative — growing the window record —
means `WIN_SIZE` × every window slot and a stride change that has broken this
codebase once already (CLAUDE.md, `wm_idx2ptr`).

So one new slot, read-and-clear:

```
OSAPI_ARG_FILE   out CF=1  no document
                     CF=0  SI = NUL 8.3 name in KERNEL_SEG (ES is already
                                KERNEL_SEG on entry, per SPEC.md 20.2)
                           DX = the document's directory cluster
                           BL = its drive
```

The app then calls the **existing** `OSAPI_FILE_GOTO` (0x0230 — the same DX/BL
pair `OSAPI_FILE_HERE` answers) and `OSAPI_FILE_READ`. No new file plumbing,
and the SDK gains one call rather than a contract.

That the answer is a locator rather than "the kernel has already put you in the
right directory" is forced, not a preference: `ld_run_body` reads the app's
image out of the *app's* directory and far-calls the entry as one unit, so the
kernel cannot be standing in the document's directory when the entry proc runs.
Handing over the locator moves that one `dsk_chdir` to where it can happen.

Read-and-clear, so a second instance cannot inherit the document.

### 2.4 Registration: the app supplies its own file stem — **recommended**

```
OSAPI_ASSOC_SET  ES:SI -> 3 extension bytes + 8 stem bytes (an X stub:
                          the kernel needs the caller's DS to read them)
                 out CF=1 = table full and nothing was stored
```

The kernel cannot supply the stem itself: `I_NAME` holds the 16-byte **header**
name (`SOLITAIRE`), not the 8.3 file name (`SOLITAIR.O88`), and nothing in the
instance record records where the package was loaded from. The app knows both
at build time; an `OS88_ASSOC 'BMP','PAINT'` macro makes it one line.

There is deliberately **no ownership model** — "take over an existing one" was
the ask, so a matching extension is repointed at the caller's app slot.
Registering an association grants nothing: the worst outcome is that the wrong
program opens a file.

Registration takes an app slot (or reuses the one already naming that stem —
which is what keeps a program registering four extensions to one slot) and
fills its glyph on the spot, out of the caller's own header at offset 32
(SPEC.md §20.2), which the kernel can read through the caller's segment.

### 2.5 The shipped glyphs are baked at build time; the rest fill in

**The first version of this plan resolved every glyph at runtime, and it failed
the one workflow that matters most.** Boot with the apps disk in B:, open
Drive B, go straight into a `DOCUMENTS` folder without detouring through
`APPS/`: `README.TXT` finds its association, finds the Notepad app slot, and
finds its glyph **unresolved** — because nothing has read `NOTEPAD.O88`'s first
sector. The document draws a bare page. Double-clicking it *works* (§2.7 rung
4 knows Notepad lives in `APPS`), so the plan opened the file in the right
program while refusing to say which program that was. Indefensible, and the
fix costs no RAM at all.

**The shipped defaults' glyphs are baked into the table at build time.** They
are knowable: `build/notepad.o88` bytes 32..95 *are* Notepad's 16×16 icon
(verified — v3, flags bit 0 set), so the 8×8 reduction can run on the host. The
8 bytes are already the app slot's glyph field, so this changes **where the
bytes come from and nothing about what they cost**. Boot, Drive B, Documents:
Notepad's mark on every `.TXT`, with no disk access, ever.

**Nothing is read at boot because the bytes are *in* `kernel.bin`.** They are
`db` bytes in `.text` — the boot sector already loads the kernel as one
contiguous run, and those 32 bytes (4 apps × 8) ride along inside the existing
512-byte image rounding, so not even one extra sector is read. The host does
the reading, once, at build time. This is the same standing that
`dsk_folder_ico` and the menu-bar logo already have: icons that live in the
image because there is nothing on disk to harvest them from at the moment they
are needed.

**A baked glyph describes the `NOTEPAD.O88` this tree built, and a two-byte
fingerprint is what makes that checkable rather than assumed.** Put a different
program of that name on a disk and the baked mark is simply wrong. The staged
directory entry already carries the file's **size** at zero cost (§19.1
offset 20, and a type-1 entry's high word is required to be 0), so baking the
expected size alongside each default — `assoc_size[12]`, 24 bytes, a fourth
parallel array — turns "assume it is ours" into "know it is ours":

- **name and size match** → the baked glyph is right, use it.
- **they do not** → treat the slot as unresolved and let §2.5's fillers
  harvest the real icon. A stranger's `NOTEPAD.O88` gets a bare page until it
  is seen, which is honest, instead of Notepad's mark, which is a lie.

A false match needs a different program with the same name *and* byte-identical
length; the consequence is a cosmetically wrong icon and never a wrong load,
because §2.7's name re-check governs the open path independently. The FAT write
timestamp would be a stronger key, but `tools/os88disk.py` **pins every
timestamp for determinism** (a released image has to rebuild byte for byte),
so it is a clean "was this written by our tool" signal rather than a unique
one — worth knowing, not worth the bytes.

This same fingerprint is the enabling half of a larger optimisation that
belongs to the disk work rather than to this plan — see
`docs/DISK-PERF-PLAN.md` §5.5, mechanism D, where a cache **hit skips the
harvest read entirely and a miss harvests as today**, so a new package still
works and then stops costing anything.

**One consequence lands back here and must not be blurred: `ASSOC.DAT` will
serve two consumers with different lifetimes.** Its *association* rows merge
into the global `.text` table of §2.2 and live for the whole session across
every volume; its *icon* rows serve the current volume's mount and are dropped
on a volume switch. One file, two sections, two lifetimes. Anything that treats
them as one thing will either leak a volume's icons into another's listing or
throw away associations on a switch.

It must be **generated, not hand-pasted**. `tools/os88mini.py` emits a `db`
line per shipped app from its `.o88`, and `kernel.bin` gains a dependency on
those four packages. Two notes: the DAG stays acyclic (packages depend on
`apps/os88api.inc`, never on `kernel.bin`), and pasted bytes would go stale
silently when an app's icon changed — a class of staleness nothing else can
catch, which is exactly why the dependency is the point rather than the cost.

For everything else — third-party packages, and any row taken over at runtime —
the glyph fills three ways, none of them extra I/O:

- **the loader** — a package with the embedded-icon flag has its body in hand
  at load; if its stem names a slot, reduce and store. So **opening one
  document of a type fixes the icon for every document of that type**, which
  makes the cold case self-healing rather than permanent.
- **the mount** — pass A already reads every type-1 file's first sector, so
  browsing the folder an app lives in lights it up.
- **registration** — out of the caller's own header.

Rejected: **resolving during the mount by going and finding the app** — see
§2.5.1, which is also a correction: it is far dearer than the two seconds an
earlier draft of this document claimed.

Until a slot is resolved its documents draw the **bare page frame**. That is a
correct, unambiguous document icon and not a placeholder, which is the same
graceful-degradation rule SPEC.md §50 asks of every claim path.

### 2.5.1 Why "go and find the app" is not four sector reads

The four icon sectors are about **3% of the cost**. The rest is the machinery
that gets the head to them, and it is worth counting because the intuition that
this is cheap is the reason it keeps looking like the obvious fix.

**`dsk_chdir` is `disk_mount`.** Not a seek, not a cheap re-point — the body is
four lines and the middle one is `call disk_mount`, so changing directory
*within one volume* re-reads the boot sector and re-snapshots the FAT window.
`dsk_chdir_q` (SPEC.md §18.9) skips the scan, the sort and the per-file icon
harvest, and skips **none of that**. `DSK_FAT_SECS` is 9.

And **`dsk_xfer` issues one int 13h per sector** — its `.sector` loop recomputes
CHS and calls the BIOS once per 512 bytes. Consecutive sectors are separate
BIOS calls, so on real hardware each one has missed the sector that was under
the head and waits a full revolution: at 300 RPM that is **200 ms a sector**,
not 200 ms a track.

One association resolved from a `DOCUMENTS` folder, same volume:

| step | sectors |
|---|---|
| `dsk_chdir_q` to `APPS` — boot sector + FAT window | 10 |
| walk `APPS`'s directory for `NOTEPAD.O88` | ~2 |
| **the icon: `NOTEPAD.O88`'s first sector** | **1** |
| the way back — `dsk_relist` → `dskw_sync`, a **full** remount of `DOCUMENTS`, scan and sort and its own icon harvest included | ~12+ |
| | **~35** |

So four filetypes in one folder is **~140 sector reads to obtain 4**, and at a
revolution each that is on the order of **7–8 seconds**, not the two an earlier
draft of this plan claimed. The correction runs the same direction as the
verdict, which is the only reason it did not change it.

The per-revolution model is reasoning from drive mechanics and **should be
measured on the XT before it is quoted as fact** — but it does not stand alone:
CLAUDE.md independently records that a `SYSTEM.CFG` write is "2+ seconds of
completely frozen UI on the floor machine (mount, data, FAT, directory, FAT,
remount)", which puts a mount at roughly a second by a route that has nothing
to do with this arithmetic.

### 2.5.2 The on-disk association cache — worth doing, and it composes

Given §2.5.1, the case for caching the answer on disk is strong: **12 apps × 16
bytes is 192 bytes — one sector.** One read replaces ~140.

`ASSOC.DAT` in a volume's root, hidden + system — **the whole table, not just
the icons**, which is why it is not called `ICONS.DAT`:

```
app rows  12 x 20   stem 8 + glyph 8 + directory cluster 2 + pad 2   240
ext rows  24 x 4    extension 3 + app index 1                         96
                    + a small header (magic, version, counts)     ~ 350 = one sector
```

The drive is not stored: it is whichever volume's file this is.

**Runtime registrations live here, not in `SYSTEM.CFG`, because an association
is bound to a disk.** An association names a program, a program is a file on a
volume, and taking that volume out of the drive makes the association
meaningless — so the machine's config file is the wrong home for it and this
one is the right one. Three consequences, all good: §6's `SYSTEM.CFG` option is
**withdrawn entirely** along with the ~190 bytes of `.bss` it wanted; the
on-disk format becomes exactly §2.2's two tables serialised, so there is one
layout rather than two; and a disk carried to another machine brings its
associations with it.

**Mounting a volume MERGES its file into the live table; it does not replace
it.** Replace would lose the apps disk's associations the moment a documents
disk went in. On a conflict the later mount wins — the same rule runtime
registration already follows for "take over an existing one" (§2.4). Entries
for apps on a volume no longer in the drive simply stay and cost nothing:
their icons remain correct, and §2.7's name re-check is what stops a stale
locator ever being acted on.

The baked defaults (§2.5) and a disk's file do not fight: the disk's row for
`PAINT` carries a **cluster**, which a baked default cannot, so it is strictly
better and wins on merge. A disk with no `ASSOC.DAT` at all — user-built, or
predating this feature — still works off the baked defaults and rung 4, and
gets a file written the first time something heals.

**Registration writes too, and that is what makes a third-party association
survive a reboot.** A runtime registration lives in `.text` RAM and dies with
the power, so if the only write trigger were "heal after a miss", a package's
association would be gone by the next session and the user would have to run
it again to get its icons back. So the triggers are **two**: a heal, and a
registration that changed the table. The second is affordable for the same
reason the first is — it lands immediately after `ld_run_body` returns from a
load the user has just double-clicked and watched, on the UI task where
`ui_tm_open`'s volume banking already happens, **not inside the entry proc**
and not on a click that expects to be instant. SPEC.md §31.8 is honoured by
where it sits, not by luck.

**The cluster is in the row because of the move case, and it is the reason the
runtime writer is not optional.** A shipped app that gets moved keeps its baked
glyph (that lives in `kernel.bin`) and loses its *location* — so without a
writable cache, every session after the move pays §2.5.1's full search again,
for ever. The cache is the only place a discovered location can outlive a
reboot; §2.2's `assoc_clus` hint is `.text` RAM and dies with the power.

Read **once when the volume is first mounted in a session**, straight into
§2.2's table; after that the RAM table serves every lookup. §19.6's
`dskw_write_sys` already exists precisely so the kernel can rewrite a hidden +
system file, so the write plumbing is not new.

Five things about it:

- **Build it on the host.** `tools/os88disk.py` already places every `.o88` and
  knows the cluster it placed each one at, so it can write a correct, fully
  warm `ASSOC.DAT` as it builds the floppy. The shipped disk then costs the
  target nothing on a machine where nothing has moved.
- **Write on a MISS, not on a move — do not hook the file operations.** The
  instinct is to update the cache when the file manager moves or deletes an
  app, and that is both more work and less complete: it catches only the moves
  *this OS* made, and the case actually worth surviving is a file moved from
  DOS, from another machine, or by a rebuild. Healing at the point of discovery
  covers all of them with one code path — the search has just paid ~35 sectors
  to learn something the cache did not know, so writing it back is cheap
  *relative to what was just spent*, and it happens once per app per move
  rather than once per paste. It also means a **deleted** app cleans itself up
  the next time one of its documents is opened, with no delete hook either.
  This is the one place where doing less is also doing more.
- **The cache may serve icons on faith; it must never serve the open path.** A
  stale hit — someone replaced `NOTEPAD.O88` with a different program of the
  same name — is a cosmetically wrong icon, which is harmless, and would be a
  *wrong program loaded*, which is not. So the locator still goes through
  §2.7's name re-check on the disk, every time. That is the same boundary the
  cluster hint already draws, and it is the one invariant this feature must not
  lose.
- **A miss is ordinary.** An app moved or deleted behind the OS's back means
  the stem is not where the cache implies; that falls through to §2.7's rungs
  exactly as an empty hint does. No new failure mode.
- **The write is affordable only because of where it sits.** It is the 2+
  second frozen-UI sequence CLAUDE.md quotes, and SPEC.md §31.8's rule is that
  no such write may land on a click. It does not: it lands immediately after a
  search the user has *already* waited seconds for, on the rare path, and it is
  what stops that wait recurring. That is the opposite of the Control Panel
  case the rule was written for — there the work was already done and only the
  record waited; here the record is the entire point. Worth stating in SPEC.md
  as an explicit exception rather than leaving it to look like an oversight.
- **A failed write is a normal outcome.** A write-protected disk — the shipped
  boot floppy on all seven 86Box machines carries `wp://` deliberately — means
  no cache, so every session re-searches. Degradation, not an error, and
  nothing user-visible.

It **composes with the build-time bake rather than replacing it**: a fresh or
foreign disk has no cache, and the shipped set must be right on the first boot
of any machine. The bake covers the four shipped apps with zero I/O forever;
the cache covers everything else with one sector.

This is Phase 2c in §6.

### 2.6 Nothing is harvested at boot, and that is the whole point

**Boot cost of this feature is zero disk reads.** Nothing walks the apps disk
looking for icons — not at boot, not ever. `drv_boot` mounts A:, whose only
*visible* type-1 file is `TASKMGR.O88` (SPEC.md §19.6 hides the kernel, the
drivers and `SYSTEM.CFG`), so the boot mount's harvest is one first-sector read
that already happens today. All this feature adds to it is a table scan per
listed entry — a few hundred cycles against a floppy access.

What that avoids is worth stating, because "harvest the app icons at boot" is
the obvious design and it is unaffordable. Calculated from 5.25" drive
mechanics — **not measured, and worth checking on the XT before it is quoted**:
300 RPM is a 200 ms revolution, so ~100 ms of average rotational latency;
average seek across 40 tracks at a 6 ms step is ~80 ms plus ~15 ms of head
settle. That is **~150–200 ms per first-sector read once the motor is up**, and
a spun-down motor adds most of a second. Thirteen shipped packages across two
folders on the *other* volume is thirteen reads plus the folder mounts plus two
volume switches: **on the order of 2.5–3 seconds of grinding at every boot**,
for icons most sessions never look at.

And on a **single-drive machine it is not merely slow but impossible** — the
apps disk is not in the drive at boot, it is what the user swaps in later.

So resolution is lazy and opportunistic (§2.5), and the icons cost their
~150 ms each exactly once, inside a mount the user asked for anyway.

### 2.7 Where the program is, and what happens when its disk is out

Three cases, and the first version of this plan got the second and third wrong.

**Resolution order** on a double-click, first hit wins:

1. **The hint** — `assoc_clus`/`assoc_drv` (§2.2): one `dsk_chdir_q` and a
   `dskw_stat` for `<stem>.O88`.
2. **The current directory** — a document beside its program is the common
   case and costs nothing at all.
3. **The volume root.**
4. **The folder `assoc_dfold` names**, for the shipped set only — `APPS` and
   `GAMES` are known at build time *for those apps*, so they are data in the
   default, not a hard-coded search path. This is the rung that carries the
   §2.5 scenario: straight from `DOCUMENTS` to Notepad, having never browsed
   `APPS/`.

**A hint must be validated by name, always.** A cluster is only meaningful on
the disk it came from; after a swap, cluster 47 is something else entirely. So
the hint is a place to *look*, never an answer — `dsk_chdir_q` there and
confirm `<stem>.O88` is present, and fall through if it is not. A stale hint
must never load the wrong file, and the name check is what makes that
structural rather than careful.

The hint is filled wherever the app is **seen**, all three free of extra I/O:
the mount's pass A (which knows the current drive and `[dsk_cwd]`), the loader,
and registration. In practice it is populated by the user having browsed to the
program once — which is how the program got onto their disk in the first place.

**Q3, an app in neither `APPS` nor `GAMES`:** covered by 1–3 above once it has
been seen, and *not* covered on a cold first double-click. Hard-coding a folder
list was the wrong instinct — this codebase's own rule is that nothing may be
built on a fixed listing position (SPEC.md §19.4). The complete fix is a
bounded tree walk, and `filecp.inc` already has the machinery for it: an
explicit frame array with `FCP_MAXD` = 6 and **no call stack**, because a task
stack cannot fund recursion. That is Phase 2b in §6 — ~150 bytes and ~1 second
for a case the hint usually pre-empts, so I would not ship it in v1.

**Q2, the program's disk is not in the drive:** the tree already makes this
*safe*, and the first version of this plan made it *rude*. `desk_init` keeps a
volume row live for a drive the machine does not have, and the comment there is
explicit that "a mount of an absent drive is an ordinary failed mount" — so the
search fails cleanly, no hang, on a one-drive XT as much as anywhere. What was
wrong was the message: "Program not found" is a lie when the program exists and
the *floppy* is out. That case needs its own notice naming the program and the
disk to insert — the Macintosh answer, and one string.

Two consequences worth having in mind:

- **The icons survive the disk leaving the drive.** The glyph is cached in the
  app slot, not resolved on demand, so browsing `APPS/` once and then swapping
  to a documents floppy still shows Paint's mark on every `.BMP`. That is a
  property of caching in the table rather than only in `disk_icons`, and it is
  most of why §2.2 spends 8 bytes a program.
- **A machine that has never seen the apps disk shows bare page icons**, and
  they are correct — an unresolved association is still a document. It is
  degradation, not breakage, exactly as SPEC.md §50 asks of a refused claim.

### 2.8 A package declares its own extensions — and it is not a version bump

**The correction first: this does not need v5, and it invalidates nothing.**
An earlier note here called it "a v5 header bump" on the assumption that bytes
96.. were free. They are not — the layout is header 32, then the icon at 32..95
*if* flags bit 0, then the code, which is why `os88pkg.py` computes
`entry_min` as 0x60 with an icon and 0x20 without. There is no spare region.

What there is instead is **flags bit 1**, and that turns out to be enough for a
change that is compatible in both directions:

```
flags bit 0   an embedded 16x16 icon follows the header      (existing)
flags bit 1   a 16-byte ASSOCIATION BLOCK follows the icon   (new)
              +0   1   count, 0..5
              +1  15   five 3-byte extensions, uppercase, space-padded
```

- **An old kernel loading a new package just works.** Everything it reads is
  unmoved — magic, version, flags, link, entry, sizes, the dispatcher at 12,
  the name at 16..31, the icon at the fixed 32..95 — and `LD_H_ENTRY` is an
  absolute offset, so where the code starts is *told*, not derived. The
  association block is inert bytes inside an image that is loaded whole.
- **A new kernel loading an old package just works**, because bit 1 is clear
  and there is no block.

So no `.o88` is invalidated, which is worth having: SPEC.md §20.8 rule 4 exists
because renumbering costs every package at once, and this change costs none.
The format version stays **3**. (Version 4 is the driver's, §51; a bump would
have gone to 5 — that part was right, it is simply not needed.)

**The one gate is the packer.** `os88pkg.py` rejects `flags & 0xFE` as reserved
bits, so it must learn bit 1, validate the block (count ≤ 5, each extension
0x21..0x7E and uppercase, **`O88` refused** — a type-1 file never consults an
association and a package claiming its own extension would burn a slot for
nothing), and extend `entry_min` by 16 when the bit is set. `OS88_ASSOC16` /
`OS88_ASSOC16_END` bracket it in the SDK the way `OS88_ICON16` already does,
asserting the block's start and end offsets so a miscounted `db` fails at
assembly rather than at mount.

**What it buys is exactly the case that motivated it.** The mount's harvest
already reads the first sector of every type-1 file, so a folder holding
`WIDGET.O88` *and* `README.WGT` teaches the OS, in that one existing read, the
extension, the glyph and the app's cluster — all three, for free. The document
next to its program opens on the **first** double-click, with no search, no
prior run, and no visit to wherever the program "should" live. The program
still has to be *seen* — its directory has to be mounted — but seeing is what
browsing already does, and running it is no longer the price of admission.

**The sticky bit stops a declaration stealing a user's choice.** A header
declaration arrives on every mount of that folder, so without a guard it would
silently take back an extension the user had deliberately reassigned. §2.2's
ext slot is extension(3) + app index(1), and 12 apps need four bits — so
**bit 7 of the index byte** is the flag, at no cost:

| source | may take a slot | sets sticky |
|---|---|---|
| build-time default | if free | no |
| header declaration at mount | if not sticky | no |
| `OSAPI_ASSOC_SET` at runtime | always | **yes** |

`ASSOC.DAT` carries the bit, so a user's override survives a reboot and is not
undone the next time the app's folder is browsed.

### 2.9 1bpp

Icons are already colourless — `icons.inc` is a mask pass and a data pass, and
the page frame is 1px black on white, so this feature has no dither class
anywhere in it and cannot trip §48's "text must come from the WHITE class"
trap. §47's rule still binds the other way, though: **it is not done until it
has been looked at on CGA and on Hercules**, because an 8×8 inset inside a
16×16 frame leaves four pixels of margin, and four pixels is where a one-pixel
error shows.

---

## 3. Phase 1 — the table and the icon

No change to what double-clicking does. Icons only.

0. `tools/os88mini.py` (new): 16×16 icon out of a `.o88`'s bytes 32..95 →
   the 8×8 majority reduction → a `db` line, and a Makefile rule generating
   `build/associco.inc` from the four default apps with `kernel.bin` depending
   on it (§2.5).
1. `kernel/assoc.inc` (new, included after `disk.inc`): the table in `.text`
   `%include`ing the generated glyphs, `assoc_find` (extension → slot or CF=1),
   `assoc_reduce` (16×16 data plane → 8×8 majority), `assoc_compose` (row →
   a 64-byte body in scratch), and `assoc_note_app` (this stem is here, fill
   its glyph).
2. `disk.inc`: the page frame body in `.text` next to `dsk_folder_ico`; the
   harvest split into pass A (unchanged + `assoc_note_app`) and pass B
   (type-0 → `assoc_find` → `assoc_compose` → `dsk_put_icon_k`).
3. `loader.inc`: `assoc_note_app` on a successful load.

Scratch: the compose needs a 64-byte buffer in the kernel segment.
**`dsk_ico` is a candidate to reuse** — it is `dsk_get_icon`'s staging buffer,
and nothing calls `dsk_get_icon` during a mount — which saves 64 bytes of the
footprint. It couples two modules through one buffer, so it is worth a comment
naming the invariant; I would take the saving.

Shipped defaults: `BMP`→PAINT, `GIF`→PAINT, `TXT`→NOTEPAD, `MOD`→TRACKER,
`MD`→ARTFUL. Five extensions across four apps, leaving 19 extension slots and
8 app slots free at the recommended 12 × 24.

**Verify — and this is the acceptance test for the whole phase:** boot
`make test`, open Drive B, and go **straight into a documents folder without
entering `APPS/`**. A `.TXT` there must already carry Notepad's mark; a bare
page means the bake (§2.5) is not working and no amount of browsing will tell
you that. Then the same for a `.BMP`. Crop and zoom
(`tools/shot.py --crop`) — a 16px icon change is exactly the thing CLAUDE.md
warns reads as "nothing happened" in a full screendump. Then `VIDEO=cga` and
`hercshot.py`.

## 4. Phase 2 — the open path

4. `files.inc`: in `fm_open_sel`, before the `[ld_pending]` post, if the type is
   0 and `assoc_find` hits, stage the document (drive, cwd cluster, 8.3 name)
   and the row, and post an association open instead. No hit → unchanged.
5. `ui.inc`: `assoc_run`, modelled line for line on `ui_tm_open` — bank the
   volume, search for `<stem>.O88`, `ld_run_body`, restore, report `LD_*`
   through `ui_note`, plus one new notice for "the program was not found".
6. The search is §2.7's four rungs, over the current volume and then the other.
   `dsk_chdir_q` (SPEC.md §18.9) is the right walker — it skips the scan, the
   sort and the per-file icon harvest — but it leaves the global snapshot
   **empty and owed**, and `[dsk_lstale]` must be paid on every path back to
   the event loop. That is the trap §18.9 records against `fcp_stop`, and it
   applies here identically.
7. Two notices, not one: **"…not found"** when the volume mounted and the
   program is genuinely absent, and **"Insert the disk holding PAINT.O88"**
   when the volume it should be on would not mount (§2.7).

**Verify:** double-click a `.BMP` and watch Paint open with it; double-click a
`.XYZ` with no association and confirm it still says "Bad package";
double-click a `.BMP` with the apps floppy holding no `PAINT.O88` and confirm
the not-found notice. Then the absent-disk case, which is the one QEMU makes
easy to get wrong — boot `make xt` (one drive, `vm/xt`) with only the system
disk, double-click a document on it, and confirm the *insert-the-disk* notice
rather than not-found. Then swap in the apps disk, browse `APPS/`, swap back,
and confirm the icons are still Paint's (§2.7) and the double-click now works
from the hint.

## 5. Phase 3 — registration and the document handoff

7. Two API cells at **0x02E8** and **0x02F0** (the table's next free pair —
   `osapi_table_end` is 0x02E8 today) and the `91 * 8` length assertion in
   `kernel.asm:781` bumped to 93. `OSAPI_ASSOC_SET` is an **X stub** (the
   kernel must read the caller's bytes); `OSAPI_ARG_FILE` is a plain cell.
8. `apps/os88api.inc`: both `%define`s and the `OS88_ASSOC` macro.
9. Paint reads `OSAPI_ARG_FILE` in its entry proc and opens the document
   instead of starting blank — the reference consumer, and the one that proves
   the handoff end to end.

**Note:** renumbering is not involved — these are appended past the last cell,
so no existing `.o88` is invalidated (SPEC.md §20.8 rule 4).

## 5.5. Phase 4 — icons in the Standard File dialog

The other half of "file save/load", and in scope.

10. `fdlg.inc`'s row painter gains a 16×16 icon at the row's left, the name and
    size origins shifting right by 18.

Three things make it cheap, and one needs checking:

- **`FD_ROWH` is already 16** (`fdlg.inc:65`), so a 16px icon is the row height
  exactly and no geometry moves vertically.
- **The listing is already the global mount snapshot**, read directly *because
  the dialog is modal* (SPEC.md §38.2), so `dsk_get_icon` serves it with no
  view cache and no new plumbing — and folders and the `..` row get their
  icons from the same slots for free.
- **It cannot share `fm_draw_icon16`**, which reads *this window's* view cache
  through `fmv_get_icon`. The dialog needs the same ~15 lines against
  `dsk_get_icon`, including the all-zero → `ico_app16` fallback. Duplication
  worth taking rather than parameterising a hot path for one caller.
- **The width works, with 8 pixels to spare — and that is the whole margin.**
  Checked rather than assumed: the name is drawn at `cx = 10` and the size
  column is **right-aligned at 196** (`fdlg.inc:1281`). An icon at 10..25 plus
  a 2px gap moves the name to 28, so a 12-character name (`FD_NAMEMAX`) ends
  at 124. The largest size that can exist is bounded by BPB rule 8's 65,535
  sectors — 33,553,920 bytes, **8 digits**, 64px, starting at 132. Eight pixels
  of clearance. So a 16px icon fits and an 18px one does not; anything that
  widens the inset must move the size column instead.

**Verify:** open Paint, File > Open, and confirm the `.BMP` rows carry the same
document mark the Disk window shows — the two read the same snapshot, so a
disagreement means one of them is not using it.

## 5.7. Phase 5 — packages declare their own extensions (§2.8)

11. `apps/os88api.inc`: `OS88_ASSOC16` / `OS88_ASSOC16_END`, bracketing the
    16-byte block and asserting its start and end offsets the way
    `OS88_ICON16` already does — a miscounted `db` must fail at assembly, not
    at mount.
12. `tools/os88pkg.py`: allow flags bit 1, extend `entry_min` by 16 when it is
    set, and validate the block — count ≤ 5, extension bytes 0x21..0x7E and
    uppercase, `O88` refused.
13. `disk.inc` pass A: when bit 1 is set, the block is already in
    `dsk_secbuf` — the harvest read it. Parse it, take each extension's slot
    unless sticky (§2.8), and fill the app slot's stem, glyph and cluster from
    what pass A already knows.
14. The sticky bit in `OSAPI_ASSOC_SET` and in `ASSOC.DAT`'s ext rows.

**No version bump and no rebuild-the-world.** Format version stays 3 and every
existing `.o88` keeps loading (§2.8). Ship it on one package first — `hello` is
the natural choice, since it is the tree's deliberate iconless case and will
prove the block works with flags bit 0 *clear*, which is the layout arithmetic
most likely to be got wrong.

**Verify:** put a `.WGT` document and a `WIDGET.O88` that declares it in the
same folder, on a disk the kernel has never seen, and double-click the
document **first**. It must open, with the right icon, on the first click — no
prior run of `WIDGET`, no search. Then reassign `.WGT` to something else at
runtime, re-browse that folder, and confirm the declaration does **not** take
it back (§2.8's sticky bit).

## 6. Optional phases, each with its own cost

- **~~Persistence in `SYSTEM.CFG`~~ — WITHDRAWN**, and not merely deferred:
  `ASSOC.DAT` (§2.5.2) is the better home, because an association is bound to
  the disk its program lives on rather than to the machine. `SYSTEM.CFG` does
  not grow, `CFG_NB` stays 81, and the ~190 bytes of `.bss` it would have cost
  every machine are not spent.
- **Phase 2c: the on-disk association cache** (§2.5.2) — **not optional, and it is
  reader *and* writer.** One sector in place of ~140. The host half is nearly
  free (`tools/os88disk.py` writes `ASSOC.DAT` as it builds the floppy, so a
  shipped disk arrives warm), and the runtime writer is what keeps it warm once
  anything moves — including the shipped apps, whose baked glyph survives a
  move but whose *location* does not. Reader ~100 bytes (the row staging can
  borrow `dsk_secbuf`), writer ~150.
- **~~Phase 2b: the bounded tree walk~~ — DROPPED.** It existed for a program
  in a folder nobody has browsed and no default names. `ASSOC.DAT` covers that
  case properly: the program must be run once to register its association at
  all, running it is what fills its cluster, and §2.5.2's registration trigger
  writes both to the disk — so the second session opens it from the cache with
  no search. The tree walk would only have served a program that had *never*
  been run, which by construction has no association either. ~150 bytes saved
  and `filecp.inc`'s frame array stays a copy-engine concern.

  What that left — a third-party association requiring the program to be *run*
  once — is now **fixed by Phase 5** (§2.8/§5.7): a package declares its
  extensions in its header, the harvest reads that sector already, and a
  document beside its program opens on the first double-click. The program
  still has to be *seen*, which browsing does; it no longer has to be run.
- **~~A second document into a running app~~ — SETTLED, and it needs no code.**
  A second document opens **another instance**, which is what the pull model
  already does by construction: every document open is a launch. Replacing an
  open document with a clicked one would destroy work the user did not ask to
  lose, and no package in this tree can hold two documents at once anyway.

  The cap worry that made this an open question was misplaced: `inst_kinds`'
  caps are **built-in kinds only** (`instance.inc:104`) and a package is not in
  that table, so package instances are bounded by `INST_MAX` = 12 and the heap.
  Past either, the load fails with the existing `LD_ENOMEM` — an honest
  message on a path that already exists. So there is nothing to build and no
  push hook to regret: §2.3's read-and-clear is the whole mechanism, and its
  "a second instance cannot inherit the document" rule is what makes two open
  documents independent.

## 7. What this does *not* change

`fm_draw_icon16`, `icon_draw16`, `ico_core`, the view caches, `fm_repaint`,
`wm_paint_dmg`, the dock, the Task Manager, and every §11.3 clip path. No
redraw path gains an instruction and no paint gains a disk read.

## 8. Budget estimate

Against the **1,536 bytes** of §1.4, less the 56 already paid for by the image
rounding. These are estimates from comparable routines in the tree, not
measurements — the guard is the arbiter and Phase 1 should be measured before
Phase 2 is written.

| item | section | est. bytes |
|---|---|---|
| the two tables, 12 × 16 + 24 × 4 (§2.2) | `.text` | 288 |
| the hint + default-folder arrays (§2.2) | `.text` | 48 |
| hint validation + the second notice (§2.7) | `.text` | ~60 |
| `ASSOC.DAT` reader + merge + name (§2.5.2, staging borrows `dsk_secbuf`) | `.text` | ~140 |
| `ASSOC.DAT` writer — heal and registration (§2.5.2) | `.text` | ~150 |
| the dialog's icon column (§5.5) | `.text` | ~80 |
| the header block's parse in pass A + the sticky bit (§2.8) | `.text` | ~80 |
| `assoc_size` + the baked-glyph fingerprint check (§2.5) | `.text` | ~50 |
| the page frame body | `.text` | 64 |
| `assoc_find` / `assoc_note_app` | `.text` | ~110 |
| `assoc_reduce` (majority 2×2) | `.text` | ~70 |
| `assoc_compose` | `.text` | ~60 |
| harvest pass B | `.text` | ~50 |
| `assoc_run` + the search | `.text` | ~180 |
| `fm_open_sel` branch + pending state | `.text` | ~50 |
| 2 API cells + the X stub | `.text` | ~40 |
| notice strings | `.text` | ~40 |
| compose scratch (0 if `dsk_ico` is reused) | `.bss` | 0–64 |
| **total** | | **~1,590** |

**The budget was raised to cover this.** `KERN_BUDGET` 76,288 → **78,336**
(+2,048), asked for and granted to fund this plan and `docs/DISK-PERF-PLAN.md`
together — ~1,590 here and ~200 there against the 1,536 that were spare, which
the two do not fit. Spare after both: ~1,750.

The decisions taken since the grant moved this figure by +200 net: the dialog's
icons ~80, the cache's merge ~40, and Phase 5's header parse plus the sticky
bit ~80. Two savings do not appear in the table because they were never in it —
the `SYSTEM.CFG` key was withdrawn before it could cost ~190 bytes of `.bss` on
every machine, and dropping Phase 2b left ~150 unspent.

Three things that follow from the grant rather than being excused by it:

- **The 12 × 24 table sizing stands.** The 8 × 16 fallback (−96) is back to
  being a reserve, not the baseline.
- **Phase 1 is still built and measured against the guard before Phase 2 is
  written.** Every figure above is a guess from comparable routines, and the
  grant does not make them measurements.
- **The raise lands with the first commit that needs it**, not with this
  document. A raised guard with nothing spent under it is precisely the "guard
  switched off" failure the fifth (downward) move exists to record — and the
  sixth move's own comment blames a *stale figure in a doc* for letting three
  features spend headroom without the constant being revisited.

`docs/DISK-PERF-PLAN.md` §9 carries the same note; the two plans share one
grant, and the seventh entry in `kernel/kernel.asm`'s comment should name both.

### 8.1 The "fix the disk instead" argument, and what is left of it

An earlier draft of this section claimed that fixing FIELD-NOTES note 3's
same-volume `chdir` "may well be smaller than the code it lets `assoc_run`
avoid". **That is wrong twice over and is corrected here rather than quietly
deleted, because the shape of the error is the useful part.**

**Wrong 1: a fast path adds code, it does not remove any.** Both spends land
against the same `KERN_BUDGET`. Fixing `disk.inc` cannot fund `assoc.inc`;
there is no ledger in which speed work in one module buys footprint in
another. The sentence read as though there were.

**Wrong 2: the fix does not retire the cache.** Count it. Today one resolution
is ~35 sectors (§2.5.1). With a same-volume fast path the boot sector and the
nine FAT sectors go from both the outbound `chdir_q` and the return `relist`,
leaving the `APPS` directory walk (~2), the icon (1), and re-scanning
`DOCUMENTS` on the way back (~2) — call it **5 sectors, so ~20 for four
filetypes**. Better by 7×, and at mechanism C's revolution-per-sector that is
still **~4 seconds**. `ASSOC.DAT` at one sector still wins decisively, so the
250 bytes stay.

**What survives is a conditional, and it needs both halves of note 3.** With a
same-volume fast path *and* multi-sector reads, those 20 sectors become a
handful of int 13h calls in a few revolutions — under a second, at which point
the cache is genuinely arguable and its reader, its writer, the hint arrays and
their validation (~360 bytes together) become a UX preference rather than a
necessity. That is a real prospect. It is two fixes and a judgement call, not a
free win, and nothing in this plan should be sequenced as though it were.

**What survives unconditionally is where the bytes are best spent.** A
same-volume fast path looks small — `dsk_fatw_pick` already carries the exact
safety rule ("only a QUIET mount may reuse a banked window; a full mount is a
re-validation, the disk may have been swapped"), and §18.8.1 already banks a
window per driver-backed volume. A floppy is excluded not by a correctness
argument but because it has no claim to bank into, and its window is `FAT_SEG`
— resident, and by that section's own reasoning never sliding. So the policy,
the buffer and the swap rule all exist; what is missing is letting a quiet
same-volume mount reuse what is already in memory. Those bytes fix a reported
symptom in every operation the OS performs. The cache's bytes work around that
symptom for one feature and carry an invariant (§2.5.2: never serve the open
path) that must hold for ever. **If only one of the two gets spent, it should
be the disk.** The build-time glyph bake (§2.5) is in this table
at zero — it changes where the app slot's 8 bytes come from, not what they
cost — and `tools/os88mini.py` runs on the host. That is affordable and it is not nothing, and per
CLAUDE.md the decision to spend it belongs with whoever wants the feature —
not with the build.

The lever if it comes out tight is the table sizing alone, and it is two
constants: 8 × 16 gives back 96 bytes and still allows 16 associations —
double the flat design this replaced. Phase 1 should be measured against the
guard before Phase 2 is written.

## 9. Settled, and what is left

**Settled** (these were §9's open questions and are now decisions, recorded
here so the reasoning is not lost when the sections above read as if they were
always this way):

- **Persistence → `ASSOC.DAT`, not `SYSTEM.CFG`** (§2.5.2). An association is
  bound to the disk its program lives on, not to the machine. `SYSTEM.CFG` is
  untouched and ~190 bytes of `.bss` are not spent.
- **The dialog's icons are in scope** — Phase 4, §5.5.
- **Phase 2b, the tree walk, is dropped** (§6). `ASSOC.DAT` covers the cold
  third-party case once the program has been run once, which it must be
  anyway for its association to exist at all.
- **A second document opens another instance** (§6). No cap applies to
  packages and no code is needed.
- **12 apps / 24 extensions stands**, the grant having removed the pressure to
  shrink it.

**Still open:**

1. **Full icon + corner badge instead of the 8×8 inset?** (§2.1) More
   recognisable at 8× the cached RAM — 12 apps × 64 bytes is 768. The grant
   makes it *affordable* where it was not before, so this is now a taste
   question rather than a budget one, and it should be answered by looking at
   both on a 1bpp adapter (§2.9) rather than on paper.
2. **Nothing else.** (The header declaration is decided and is Phase 5,
   §2.8/§5.7.)

## 9.1 The icon prototype — the last thing, deliberately

§2.1's choice between the **8×8 inset** and the **full icon + corner badge** is
settled by looking, not by argument, and the grant has made both affordable —
the badge's 12 × 64 = 768 bytes was the whole objection to it.

So: build everything else first, then produce **both** and compare. The inset
is the plan of record and the one to build; the badge is a second
`assoc_compose` and a second page body, no other change, because §1.1's
decision to compose into `disk_icons` means the choice is confined to one
routine. Prototype output must include a Disk window in **both views** (list
and icon grid) and the §5.5 file dialog, on **VGA and at least one 1bpp
adapter** — CGA and Hercules differ from VGA in kind, not just in depth, and
four pixels of page margin is exactly where that shows.

Whichever wins, the loser's ~60 bytes come back out.

## 9.2 Order of work, across both plans

The two plans interleave and the order matters for three reasons, so it is
fixed here rather than decided commit by commit:

1. **DISK-PERF Phase 0 — count.** Cheap, and every later judgement in either
   plan is measured against it.
2. **DISK-PERF Phases 1–3.** They make §2.5.1's search cheap, so the
   association work is built on a foundation that has stopped moving. They also
   touch `disk_mount` and `dsk_xfer`, which ASSOC Phase 1 touches too —
   serialising them keeps two sets of changes out of one harvest loop.
3. **ASSOC Phases 1–5**, in order, with Phase 1 measured against the guard
   before Phase 2 is written.
4. **DISK-PERF mechanism D**, which needs `ASSOC.DAT` to exist (ASSOC Phase 3).
5. **The icon prototype** (§9.1) and the decision it settles.

**The budget raise does not land until step 3.** DISK-PERF's ~380 bytes fit
inside the 1,536 that are spare today, so moving `KERN_BUDGET` before ASSOC
Phase 1 would be raising a guard nothing has yet pushed against — which is the
failure the fifth move exists to record. The seventh entry in
`kernel/kernel.asm`'s comment goes in with the commit that first overruns.

**SPEC.md goes first within every phase**, not after it. CLAUDE.md is binding:
"Update SPEC.md *before* changing any interface, not after." §10 lists what
each phase owes it.

## 9.3 A gate package, which the plan did not have

`tests/` is where this tree proves a capability mechanically — `fmtest` for FM,
`sbtest` for the streams, `filetest` for the write path, `stackprobe` for the
stack margin — and none of the phases above has one. **`tests/assoctest`
should exist**, riding its own scratch image (`make test
TESTAPPS=build/assoctest.img`) and asserting the parts a screenshot cannot:
that `OSAPI_ASSOC_SET` takes and refuses correctly at the table's cap, that
`OSAPI_ARG_FILE` answers once and then reports empty, that a second instance
does not inherit the document, and that the sticky bit survives a re-browse.

It belongs on the `testing` branch while it is being written, per CLAUDE.md,
and nothing under `tests/` is tracked or ships.

## 10. SPEC.md

SPEC.md is the binding contract and it is updated **before** any of this is
written, not after: a new **§54, File type associations**, covering the two
table layouts and the sticky bit, the harvest's two passes and their ordering
rule, the four resolution rungs and the name re-check, `ASSOC.DAT`'s format
and its two write triggers, `OSAPI_ARG_FILE`'s read-and-clear contract, and
the two new slot numbers.

Four existing sections gain a sentence each:

- **§19.1** — a type-0 icon slot is no longer always blank.
- **§20.2** — flags **bit 1** and the 16-byte association block, with the note
  that the format version stays 3 *because* nothing an older kernel reads has
  moved (§2.8).
- **§22** — the open path's association branch.
- **§31.8** — `ASSOC.DAT`'s writes as a stated exception to "never write on a
  click", with the reason they are not one: they land after work the user has
  already waited seconds for, and the record *is* the point rather than an
  afterthought to work already done (§2.5.2).
