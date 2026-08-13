# Naming the folder you are standing in

**Status: BUILT, for KERN_BIG only — SPEC.md §22.16.** kern_small keeps the
caption it shipped with and is **byte-identical** with and without the change.
§9 is what the build found; the estimates in §6 are left as they were written,
with the measured figures beside them.

**Cost, measured:** `.text` +11, `.bss` +165, `.cold` +349 — **no rung crossed
anywhere**, `KERN_SIZE` unchanged, so it costs the machine nothing. The
estimate in §6 said "likely one 512-byte step"; it landed inside the rungs
already open. What it did spend is the cold rung's slack, which is **14 bytes**
now, and that is the number the next cold addition pays against.

The one-line summary: **the folder's name is free on the way DOWN and the
kernel throws it away, so on the way UP it has nothing left to say.** This is
not "go and find out what this folder is called" — it is "stop discarding what
you were just told".

---

## 1. What is actually wrong

Two places, and the second is worse than the first.

**The Disk window loses the name going up.** Descend into `SYSTEM` and the
window is titled `SYSTEM`; press Backspace and the window that arrives at the
parent is titled **`Folder`**. Reproduced on `os8088_5150_cga_gla` off the
shipped 360KB pair, walking `B:` → `SYSTEM` → `DOS` and back:

| step | caption |
|---|---|
| `B:\` | `Disk` |
| `B:\SYSTEM` | `SYSTEM` |
| `B:\SYSTEM\DOS` | `DOS` |
| Backspace → `B:\SYSTEM` | **`Folder`** |
| Backspace → `B:\` | `Disk` |

**The Standard File dialog never names a folder at all** — not even one it
descended into, and not even one the kernel put it in on purpose. Its header
is the drive letter and then one of three fixed words (`fdlg.inc`
`fdlg_s_root` / `fdlg_s_sub` / `fdlg_s_nodisk`), picked by
`cmp word [dsk_cwd], 0` and nothing else. §38.10 opens an app that has chosen
nowhere in `MEDIA`, so **the first `File ▸ Open` a new user ever runs reads
`B: Folder`** while standing in a folder with a perfectly good name. Verified
on screen, Note Pad's `File ▸ Open` on a fresh boot.

So the dialog is not a smaller instance of the Disk window's bug; it is a
larger one. The Disk window at least names a folder it walked into.

## 2. Why it is like that

Both sites are deliberate and both say so in comments. `fm_settitle`
(`files.inc`) takes the name **from its caller**:

```
    xor si, si                  ; the parent cannot be named without the
    jmp fm_go                   ; grandparent's listing (SPEC.md 22)
```

That is `fm_c_up`, and the double-clicked `..` row (`fm_open_sel`'s `.folder`,
gated on `[fm_ftype] == 3`) carries the identical comment. The reasoning is
correct as far as it goes: a directory records its own parent's **first
cluster** in its `..` entry (§19.2, which is what `dsk_dotdot` reads), and a
cluster is not a name. To turn it into one you must list the **grandparent**
and find the entry whose first cluster matches — a whole extra mount to print
a string.

`fm_reload` (Refresh) declines for the same reason and is **right to**: nothing
moved, so the caption it already has is correct. That path needs no change and
must not get one.

## 3. Three designs that do not survive contact with the redirector

Worth writing down, because two of them are the first thing anyone reaches for
and the third is what a FAT-only reading of the problem produces.

**(a) Walk the grandparent on demand.** Correct on FAT, and it costs a mount:
a floppy mount is **~12 sectors regardless of what is in the directory**
(§18.8.1's measurement — BPB 1 + FAT 9 + dir 1 + `ASSOC.DAT` 1), which on the
calibration machine is seconds of motor, under the gfx lock, to change a word
in a title bar. It is also **impossible on a redirected volume**: §62.9.1 makes
the handle *opaque to the kernel* — "a directory ordinal, a hashed path, an
index into its own table" — so the kernel may compare handles for equality but
cannot use one to find an entry, and the grandparent listing it would search is
a round trip of its own.

**(b) A kernel-side (cluster → name) cache, filled during `disk_mount`.**
Tempting, because every listing already walks past exactly the names and
clusters this wants, and one table would serve all four Disk windows and the
dialog at once. It fails on **identity**: a cluster is reused the moment a
folder is deleted and another created, so the cache can name a folder with its
predecessor's name — a wrong name, silently, which is worse than no name. It
would need invalidation on every write on every volume, and a handle-keyed row
for a redirected volume is only as stable as the far side's table (§62.10.2
dedupes on `(parent, name)` precisely so a cached handle survives, which is a
promise about *reuse*, not about *identity after a delete*).

**(c) Ask the volume.** A `FSV_NAME` verb would work on the redirector — the
DOS side stores `(parent, 8.3 name)` per handle and already rebuilds paths by
walking up (§62.10.2) — but it is a fourteenth verb, a protocol change, and a
round trip measured in **seconds** on this cable. And it does nothing for FAT,
which would still need (a). A design that needs two mechanisms for one question
is the wrong design; §62.9.1's own reasoning for folding the parent handle into
`FSV_CHDIR` rather than giving it a verb is the precedent.

## 4. The design: remember the way in

**A per-window path of NAMES, pushed when the user descends and popped when
they go up.** It asks the volume nothing, so there is no FAT path and no
redirector path — it is a record of *the user's own navigation*, not of the
volume's identifiers, and identifiers are the only thing the two volume kinds
disagree about. That is the whole answer to "does it work with the
redirector": there is nothing in it for a redirected volume to answer
differently.

It is also nearly free, because **every descent already has the name in
hand**:

| site | what it already holds |
|---|---|
| `fm_open_sel` `.folder` | `fm_name`, staged by `fm_stage_name` — passed to `fm_go` in SI today |
| `fm_kinit` | `[fm_seed_nam]`, the seed's name |
| `fm_choose` `.inplace` | the caller's SI |
| `fdlg_dive` | the staged entry it takes the cluster out of — **and discards the name from** |

So the push is a copy of a string the routine is already looking at, and the
pop is a truncation.

### 4.1 Shape

One buffer per Disk window, in the `FS_` state block (`fm_pool`, `KD_SSIZE` is
a stride with no cap on it), plus **one** for the dialog, which is modal and so
can only ever be in one place at a time (§38's own rule — it is why the dialog
reads the global mount snapshot instead of keeping a view cache).

```
FS_PATH   resb N      ; 'SYSTEM\DOS', NUL-terminated, '\' between components
FS_PLOST  resb 1      ; levels we are BELOW the deepest one FS_PATH records
```

- **push(name)**: append `\` + name if it fits; otherwise `inc FS_PLOST`.
- **pop**: if `FS_PLOST` is non-zero, `dec` it and answer *no name*; else
  truncate at the last `\` and answer the new leaf (empty → *no name*).
- **caption**: the text after the last `\`, or *no name*.
- **clear**: on a drive change (`fm_mount`), on Root Folder, on a format, and
  on `fmv_sync`'s folder-vanished fallback — every path that moves `FS_CWD`
  without descending.

`FS_PLOST` is what keeps it honest at depth. Without it a window that
descended past the buffer's capacity would pop back into a name that belongs
to the wrong level; with it, the over-deep levels are *counted* and given
back one at a time, and the recorded path is only consulted once the count is
zero. A window deeper than the buffer holds reads `Folder` — exactly what it
reads today — and recovers its names on the way back up.

### 4.2 The invariant that makes it unable to lie

**`FS_CWD` stays the authority for "am I at the root", and the path is only
ever consulted for a name.** `fm_settitle` already tests `FS_CWD == 0` first
and answers `Disk`; everything this adds is a better value for SI on the paths
that pass 0 today. So the worst case of a path that is empty, lost or wrong is
**the caption this build already prints**, and there is no new state in which
the window can claim to be somewhere it is not.

That matters most for a *seeded* window — one opened straight onto a folder by
a drag, an association or `fm_choose`. It knows its leaf name and nothing
above it, so its path is a fragment: push the leaf, and a pop empties the
buffer while `FS_CWD` is still non-zero, which yields `Folder`. Correct, and
correct without a special case.

### 4.3 What it does NOT try to do

- **No path in the caption.** The caption, the dock tile and the Task Manager
  row are the same 16 bytes (`I_NAME`, §29.1 — 15 usable), so `B:\SYSTEM\DOS`
  does not fit and the tile is the wrong place for it anyway. The leaf is what
  a System 1 window is titled and what this fixes.
- **No rename tracking.** Rename a folder from another window while you are
  inside it and the remembered name is stale — which is exactly as stale as
  the caption is today, `I_NAME` being a copy. No regression, no new promise.
- **Refresh keeps its silence.** `fm_reload` is untouched.

## 5. The dialog, which is the bigger half

The same buffer, one instance, and **two** changes rather than one:

1. `fdlg_dive` pushes the name it is already holding — this alone fixes the
   descending case, which is every folder the dialog has ever shown.
2. The header draws the leaf where it draws `fdlg_s_sub` today.

`fdlg_s_sub` (`Folder`) stays as the fallback for a folder the dialog was
*placed* in rather than walked into — `fdlg_home_go`'s `MEDIA` on the first
open. Worth deciding explicitly and worth measuring first: the header is drawn
at a 30px pen inside a fixed-width modal, so the longest 8.3 name has to be
proven to fit beside the drive letter before the leaf is allowed there. If it
does not, the honest answer is to leave the dialog's header alone and stop at
(1) plus a widened box, not to truncate a name.

**And the `MEDIA` case argues for one more push site**: `fdlg_home_go` knows
the name it is resolving — it is a literal in the kernel — so the first open
could read `B: MEDIA` for the cost of a push at the one site that already
names the folder it is looking for.

## 6. What it should cost, and what has to be measured

Estimates, and marked as such — nothing here has been built, so
`tools/kernsize.py --modules` is what settles it.

| | estimate |
|---|---|
| `FS_PATH` at 32 bytes + `FS_PLOST`, × `FM_MAXWIN` (4) | ~136 bytes `.bss` |
| the dialog's own | ~33 bytes `.bss` |
| push/pop/leaf helper, shared | ~100–150 bytes `.cold` |
| call sites (6–8, each a load and a call) | ~40 bytes |

Against the tree as it stands: `KERN_BUDGET` **1,536 spare (3 steps)**, the
image rung with **472 bytes left**, the cold rung with **359**. So this is
likely **one 512-byte step** of the footprint and should be reported as such
rather than called free — docs/KERNEL-MEMORY.md's accounting rule.

`files.inc` and `fdlg.inc` both already have `.cold` sections, so the helper
belongs there; the buffers are `.bss` and must be, since `-f bin` zeroes
nothing and a path is meaningless before `fm_kinit` runs anyway. **32 vs 24
bytes is a real choice**: 32 holds three 8.3 components (`AAAAAAAA\BBBBBBBB\C…`
is 26), 24 holds two and a bit, and the shipped disks are two deep. Build it
with the constant named once and measure both.

## 7. How to test it

The point of the design is that it is volume-agnostic, so the test has to
cover both volume kinds, and **both are reachable in a container**:

- **FAT12 floppy** — `os8088_5150_cga_gla` with the shipped pair. `B:` →
  `SYSTEM` → `DOS` → Backspace → Backspace, and the caption at each of the
  five stops. The table in §1 is the before; every row of the after must name
  the folder except the two roots, which read `Disk`.
- **The redirector** — `RAMDISK.DRV`, which exists for exactly this
  (§62.9.5.1: "the file redirector's harness — every branch site the
  redirector added to the kernel runs on a cycle-accurate 8088 in a
  container"). `make ramseed` fills it with two levels of directory, which is
  the depth this feature is about. **A pass here is the claim in the title of
  this document**, because a RAM disk is a `DRVC_FILE` volume with opaque
  handles and no `..` on any platter.
- **The dialog** — Note Pad's `File ▸ Open` on a fresh boot must name `MEDIA`,
  and diving must name what it dived into.
- **The over-deep case** — a hand-built image (`os88disk.py` nests to any
  depth now) one level past the buffer, walked down and back up: the deep
  levels read `Folder` and the names must come *back* as `FS_PLOST` unwinds.
  This is the one that catches a wrong `FS_PLOST`, and it cannot be caught by
  any shipped disk.
- **The seeded window** — open a folder by drag-and-drop, then go up: it must
  read `Folder`, not `Disk`. That is §4.2's invariant, and getting it wrong is
  the only way this change can introduce a *lie* rather than a blank.

## 8. Why this is worth doing at all

It is a caption, and captions are cheap to dismiss. Two things make it more
than cosmetic. The window title is the **only** thing on screen that says
where a Disk window is — there is no path bar and no breadcrumb — so `Folder`
means the user's answer to "where am I?" is to press Backspace until they
recognise something. And the dialog's `B: Folder` is on the screen where the
user is choosing a file to overwrite: §38.10 went to real trouble to make each
app open in its own remembered place, and then the dialog does not say which
place that is.

---

## 9. What building it found

Three things, and the first two are the reason §7's test list was written the
way it was.

### 9.1 The over-deep case caught a real defect, exactly where it was aimed

§4.1 said `pth_pop` should "if `PTH_LOST` is non-zero, `dec` it and answer *no
name*". That is wrong, and no shipped disk can show it: spending the **last**
lost level lands back **at** the buffer's own end, which the buffer still
names. As written it would have thrown that name away and printed `Folder`
one level too high. It is one `jnz` — but it is a bug the design document
itself contained, and the only thing that could have found it is a disk built
to be deeper than the buffer.

Read out of a running guest, four 8-character levels on a hand-built image
(`AAAAAAAA\BBBBBBBB\CCCCCCCC\DDDDDDDD` is 35 characters against `PTH_MAX` 32):

```
  down into AAAAAAAA  lost=0 path='AAAAAAAA'
  down into BBBBBBBB  lost=0 path='AAAAAAAA\BBBBBBBB'
  down into CCCCCCCC  lost=0 path='AAAAAAAA\BBBBBBBB\CCCCCCCC'
  down into DDDDDDDD  lost=1 path='AAAAAAAA\BBBBBBBB\CCCCCCCC'
  up  #1              lost=0 path='AAAAAAAA\BBBBBBBB\CCCCCCCC'
  up  #2              lost=0 path='AAAAAAAA\BBBBBBBB'
  up  #3              lost=0 path='AAAAAAAA'
  up  #4              lost=0 path=''
```

The fourth push is **counted and not half-written** — the buffer is intact
behind it — and the names come back in order. **The captions alone could not
have shown this**: a descent is titled from the row it clicked, so level 4
reads `DDDDDDDD` whether or not the path recorded it, and the whole walk looks
identical with `PTH_MAX` large enough to hold it. That is why the check reads
the block rather than the screen.

### 9.2 Backspace did not work at all on a redirected volume — FIXED

**Pre-existing and unrelated to the caption. Fixed in a follow-up commit at
+9 bytes of `.cold`, under the 30 the requester allowed for it; the
alternative on the table was removing the shortcut, on the grounds that a
partially working one nobody knows about is worse than none.** `fm_c_up`
calls `dsk_dotdot`, which reads a directory **sector** to find the parent —
and a `DRVC_FILE` volume has none. It answers CF=1 and `fm_c_up` takes its
`.none` branch, so Backspace and `Nav ▸ Up One Folder` are silently dead on
the RAM disk and on the network volume. The `..` **row** works, because
`dsk_synth_up` fills it from `[dsk_fsup]` for a `DVK_FILE` volume (§62.9.1)
and `fm_open_sel` takes the handle out of the row — which is also what
`fdlg_dive` does, and why the dialog has never had this problem.

So the redirector leg of §7 was driven through the `..` row, and it passes:
`DOCS` → `DEEP` → up reads **`DOCS`**, on a volume with opaque handles and no
`..` on any platter.

The fix is `fdlg_dive`'s shape — take the parent's handle out of the window's
own cache, where slot 0 of a subdirectory is always the parent link (§19.5),
instead of asking the volume for a sector. One entry out of the listing the
window is already painting from, so it is volume-agnostic by construction
rather than by a branch, and a missing link becomes a **type test** instead of
a CF. The `fmv_sync` above it stays: a window whose `VIEW_KB` claim was refused
reads the globals, and those have to be its folder first.

Measured on the RAM disk after the fix: `DOCS` → `DEEP` → **Backspace** lands
in `DOCS` and names it, a second Backspace reaches the root, and the `..` row
still does what it always did. The floppy is unchanged, and the over-deep probe
still reads `lost=1` with the buffer intact behind it.

It applies to **both builds** — the bug is not caption-related, so it is
outside the `%ifdef`.

### 9.3 A `%else` is not free

The first cut of the `..` branch put the `%ifdef` around the wrong part and
restructured kern_small's instruction sequence — a `je` plus a `jmp short`
where a `jne` had been. Same behaviour, different bytes, on the build whose
whole reason is that its bytes are counted. **A KERN_BIG feature is not proven
by kern_small still assembling**; it is proven by kern_small being
byte-identical, and that is the check to run.

`make small` was already **broken on `elendilon` before any of this** —
kern_small is over `KERN_BUDGET` (99,328) and refuses to build. It is not
something this change caused and it is not fixed here; the byte-identity check
above was made by lifting the guard locally, building both trees, and putting
it back.

## 10. What is still owed

- **The seeded-window case** (§7's last bullet) is covered by construction —
  `pth_clear` then a one-component push, so going up empties the path while
  `FS_CWD` is non-zero and the caption is `Folder` — but it has **not been
  driven**, because reaching `fm_kinit`'s seed or `fm_choose`'s in-place move
  from a script needs a drag or the four-window cap. It is the one leg of §7
  that was not exercised.
- **§9.2's Backspace**, if it is wanted.
- The dialog's `fdlg_home_go` names `MEDIA` and nothing else; the remembered
  folder and the recovery branch still read `Folder`, which §5 called for
  deliberately.
