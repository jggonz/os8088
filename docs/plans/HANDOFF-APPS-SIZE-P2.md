# Handoff: apps size pass 2 - disk reclaim

**A PLAN AND A COORDINATION BOARD.** The sets table in 2 is edited by every
session that takes part. Everything else here is the brief.

The second apps size pass. The first (docs/reports/APPS-SIZE-P1-*.md, six
sets) measured a package by **image + bss**, which is what it costs a
MACHINE. This one measures it by what it costs a **DISK**, because the disks
are full: on the tree this pass was cut from, the 360KB system disk has
**5 clusters free** and the 360KB apps disk **1**. The next package, driver
or document that needs either disk has nowhere to go. The subject is the
packages on the system disks and the six 360KB images `all` builds, and the
measure is the **shipped file** and the **clusters** it takes on each disk it
rides.

## 1. Where the room is - the baseline

Cut from `elendilon` at `2c6edafc`, built in the 4-core cloud container.
`python3 tools/os88diskuse.py` reproduces every figure (1,024-byte clusters
on every 360KB volume).

| image | free clusters of 354 | what binds it |
|---|---:|---|
| `os8088-360.img` (system) | **5** | kernel 82, DOS 48, Paint 22, DeskCal 16, Note Pad 15, drivers 112 |
| `apps360.img` | **1** | Skies 45, Video 41, Tracker 26, Paint 22, Browser 18, TeXpad 17, ... |
| `office360.img` | 51 | PiXEL 111, Word 44, Sheet 37 |
| `games360.img` | 80 | PIXELSTEIN 46, Skies 45, DrMarco 21 + 43 of data |
| `small360.img` (kern_small system) | 55 | the whole small apps payload (SPEC.md 24.5.6) |
| `media360.img` | 145 | OS8088.V88 101, BEVERLY.MOD 42, MIDIRack 31 |
| `smallapps360.img` | 154 | |
| `network360.img` | 279 | |

**A package's disk weight is its clusters times its COPIES.** Paint is 22
clusters on three 360KB disks (66), Mines 2 on three, Note Pad 15 on two plus
the small disks. A cluster off a three-copy package is three clusters of room.

## 2. The sets - CLAIM ONE HERE

Five sets, grouped by the disk they relieve and ordered by how badly that
disk needs it. One session per set; one agent per package inside it.

| set | relieves | packages | status |
|---|---|---|---|
| **A** | the system disk | `dos`, `paint`, `notepad`, `deskcal`, `thewire`, `taskmgr`, `fontview`, `rlapply`, `mines` | **done** - docs/reports/APPS-SIZE-P2-SETA-2026-10-16.md |
| **B** | the apps disk: network and office | `browser`, `ftpd`, `telnet`, `OS88NET.COM` (`drivers/net/os88net.asm`, the DOS end of NET.DRV - SPEC.md 62.12; the one file under `drivers/` a set may edit), `texpad`, `artful`, `audio`, `calc` | **done** - docs/reports/APPS-SIZE-P2-SETB-2026-10-16.md |
| **C** | the apps disk: games | `tracker`, `wireball`, `tank`, `cyclone`, `missile`, `arkanoid`, `solitaire`, `tamegram` | **done** - docs/reports/APPS-SIZE-P2-SETC-2026-10-16.md |
| **D** | the apps disk: the owner-constrained and the small | `skies`, `video`, `fractal`, `piano`, `timer`, `midirack`, `chart` | **done** - docs/reports/APPS-SIZE-P2-SETD-2026-10-16.md |
| **E** | the office and games disks | `pixel`, `pixelstein`, `word`, `drmario` (with `DRMARCO.VGA`/`.HRC`/`.CGA`), `isotron`, `dotdel`, `gorillas` | **deferred to the next round** (the owner's call, 2026-10-16; DrMarco is getting its own pass to turn its files into parts) |

**Not in any set, and why** - each is the owner's to reopen:

- `sheet` - deferred to its rework in pass 1 (the owner's call); the rework
  takes the size.
- **Drivers and the kernel** - the brief says *apps*. The system disk's 112
  clusters of `.DRV` (`HDDTOOL` 14, `CTRL` 13, `ETHER` 12, `DISKOPS` 9,
  `SAVER` 8) are the next-largest lever after the packages and are a
  separate pass.
- **The shared includes** (`apps/os88*.inc`) - off-limits to every set, as in
  pass 1, because a shared edit moves every package's numbers under every
  other session at once. Their disk value is MULTIPLIED by every carrier on
  every disk, so each set records what it found (5) and the owner decides
  whether a shared lane runs after the sets. docs/plans/HANDOFF-APPS-SHARED.md
  is pass 1's list.

### 2.1 Set A's follow-on, owner-approved - LANDED on the pass branch, and it moves your numbers

On set A's results the owner approved three things outside the sets, run by
set A's session on the pass branch. **It has landed** (SPEC.md 20.13.8,
96.44.16.1): a parted package's image now ships packed when that saves a
sector - DOS.O88 48 -> 47 clusters, PiXEL 113,147 -> 105,467 bytes (111 -> 103
clusters on office360), C64 and Apple II 8 clusters each on their 360KB disks;
Skies, Word and PIXELSTEIN declined (no sector saved) and are byte-identical.
The kernel paid +33 resident bytes of `.cold` on each kernel for it, which
leaves kern_big's `.cold` rung ONE byte from crossing. The CP437 glyph
generator was REFUSED on a measurement (LZ4 already packs the box glyphs to
~179 bytes) and the core-in-part-0 idea was dropped by the owner (part 1 is
moved to launch programs under kern_dos). Set E: re-take your snapshot after
merging - PiXEL's 7.7 KB is set A's follow-on, not yours.

What was proposed:

- **`tools/os88pkg.py` packs the image of a package that has PARTS** (today
  that image ships raw). If it lands it is universal: every parted package's
  file shrinks - Skies, PiXEL, PIXELSTEIN, C64, Apple II and others in sets D
  and E. **After it lands, merge the pass branch and re-take your `--save`
  snapshot** before measuring, or the tool's saving is credited to your set.
- **`kerndos/` and `apps/os88cp437.inc`/`os88con.inc`** for DOS.O88 (the core
  inline in part 0, box-drawing glyphs generated at launch). The glyph change
  is OPT-IN; Telnet (set B) carries the same table and stays byte-identical -
  its share is recorded for set B to take or leave.
- **Opt-in gates in `apps/os88ui.inc`, `os88line.inc`, `os88gfx.inc`,
  `os88type.inc`, `os88data.inc`**, default off, so no package that does not
  set them changes by a byte. Set A's packages opt in; the per-package saving
  for every other carrier is listed in set A's follow-on report for each set
  to take in its own package source.

## 3. Coordinating over git

**The pass branch is `claude/apps-size-2-disk-reclaim-6rn5hh`** on `origin`
(`Elendilon/os8088`). Not to be confused with pass 1's set-2 branch, which
was called `apps-size-2` and is merged and gone. The pass branch is where
claims are made and where every finished set lands; the owner merges it to
`elendilon`. **This document's instruction is the owner's permission to push
to the pass branch** from whatever branch a session was given.

```sh
git fetch --unshallow origin 2>/dev/null; git fetch origin   # CLAUDE.md fork rule 1
P=claude/apps-size-2-disk-reclaim-6rn5hh
```

**To claim a set** - the push is the lock; whoever's push lands owns it:

```sh
git checkout -B <your-branch> origin/$P
# edit the set's row in 2: "claimed - <session id>, <date>"
git commit -am "apps size pass 2: claim set <X>"
git push origin HEAD:$P            # rejected? `git pull --no-rebase origin $P`,
                                   # re-read the table, and claim another if taken
git push -u origin <your-branch>
```

**While working**: each package's agent works in its own git worktree off
your branch and commits there; you merge each agent's branch into yours
(merge, never rebase). Push your own branch whenever you like. Do not push
half a set to the pass branch.

**To land a finished set** (after 4.4's final pass and the report):

```sh
git fetch origin && git merge origin/$P   # take the other sets' work first
make -j4                                  # rebuilds, fast tier included
git push origin HEAD:$P                   # a fast-forward of the pass branch
```

Then set the row to `done - docs/reports/APPS-SIZE-P2-SET<X>-<date>.md` in
the same push. **Where merges collide**, in this tree's experience:
`docs/INDEX.md` is GENERATED (`python3 tools/os88index.py`, then `make`
checks it), `SPEC.md` (each package owns its own section, so keep both
sides), `tests/suite.py`, `tests/textsites.txt` and `tests/movable.txt`
(ratchets - keep both sides' lines, the counts only go down), and the
Makefile's per-package rules. Keep both sides, rebuild, run the fast tier.

**A set's numbers are against the tree IT was cut from**, never the pass
baseline, so two sets' savings never count the same cluster twice: take
`os88diskuse.py --save` at your cut point before the first agent starts, and
`--diff` it at the end.

## 4. Method

### 4.1 One agent per package, in a worktree

Run the agents in parallel, each in a worktree of its own and with the brief
in 4.3 filled in for its package. Four cores take eight or nine agents
building at once, slowly; brief them to build with `make -j2`.

**Make the worktrees YOURSELF, outside `.claude/`, and do not use the Agent
tool's `isolation: "worktree"`.** Set A used it and paid twice: the harness
puts those worktrees under `<repo>/.claude/worktrees/`, where every Edit and
Write is a change inside Claude's own config directory and stops for the
owner's approval - dozens of "allow" prompts a set - and it cut them from
`a123653b`, 48 commits behind the session's branch, so every agent's BEFORE
was a different tree from the one it would be merged into. Instead:

```sh
git worktree add -b p2-<pkg> /home/user/wt/<pkg> HEAD   # your branch's tip
```

and name `/home/user/wt/<pkg>` as the agent's working directory in its brief
(it `cd`s there for every command and edits files by absolute path under it).
`git worktree remove` each one after its branch is merged.
`.claude/settings.json` carries allow rules for `.claude/worktrees/**` as a
fallback for any session that already started the other way.

Two things a worktree does not have and needs:

- **MartyPC.** `tools/os88marty.py` finds it at `<tree>/build/martypc`. Build
  it ONCE in the main checkout (`make marty`, several minutes) and have each
  agent `mkdir -p build && ln -s <main>/build/martypc build/martypc` before
  its first boot. Instances get private directories, so sharing is safe.
- **A built tree.** Each agent runs `make -j2` once in its worktree first, and
  takes its BEFORE numbers there, before its first edit.

### 4.2 What is measured

Per package, before and after, all of:

- **the shipped file**: `build/<pkg>.o88` as `make` leaves it (LZ4-packed,
  `PKGZ ?= lz4`), plus every data file the package ships beside it; and its
  **clusters on each 360KB disk it rides** (`os88diskuse.py --pkg <NAME>`);
- **the APP_SMALL arm** where there is one (Paint, Note Pad, Calculator,
  Solitaire, Task Manager, Tank): its file on the small disks, and
  `tests/unit/t_appsmall.py`'s row;
- **image + bss** from the header (`tools/os88pkgsize.py` reads it), and the
  heap claims it makes at run time - neither may grow (4.3 rule 3);
- for any hot path touched: guest cycles on MartyPC, before and after.

**Bytes are the result; clusters are the room.** CLAUDE.md's banner holds on
a disk exactly as in the kernel: a cluster is a rung. Optimise bytes, report
bytes, and report the clusters beside them as what the disks gained - but
never shape a change to land on a cluster boundary, and never call a saving
that crossed none "worth nothing". The next session's byte crosses it.

### 4.3 The agent brief

Fill in `<pkg>` and the package's own constraints (4.5), then hand it over
whole:

> You are taking **`<pkg>`** through the second apps size pass of os8088
> (docs/plans/HANDOFF-APPS-SIZE-P2.md - read 1, 4 and 5 first, and CLAUDE.md).
> The subject is **DISK ROOM**: the system disk and the 360KB apps disk are
> full, so the goal is to make the package's **shipped file** smaller - the
> LZ4-packed `build/<pkg>.o88` and any data file it ships beside it - without
> costing the machine anything for it.
>
> **Set up**: you work in `/home/user/wt/<pkg>` (the session made it; `cd` there for every command and edit files by absolute path under it). `mkdir -p build && ln -s /home/user/os8088/build/martypc
> build/martypc`, then `make -j2`, then record the BEFORE numbers (file
> bytes, clusters per disk via `python3 tools/os88diskuse.py --pkg <NAME>`,
> image and bss, the APP_SMALL arm if any) before the first edit.
>
> **Rules** - they are the pass's, and a change that breaks one is not taken:
>
> 1. **No feature removed and nothing user-visible changes**, except a defect
>    you fix, which you record. Pixel-identical pictures are the default.
> 2. **No hot path gets slower.** Anything a frame, a keystroke, a stroke, a
>    playback or a game loop runs is measured in MartyPC guest cycles before
>    and after if you touch it. Work moved to the ENTRY proc (a table built
>    at launch instead of shipped) is allowed and is measured: quote its cost
>    in milliseconds at 4.77 MHz, and FLAG anything over 100 ms to the owner
>    rather than taking it silently.
> 3. **RAM must not grow.** Moving bytes from the image to bss is neutral
>    (the claim is image + bss) and is the usual shape of a disk win; a net
>    rise in image + bss or in any heap claim is stated with its numbers and
>    a reason, and on an APP_SMALL arm it is not taken at all.
> 4. **Your package only.** `kernel/`, `drivers/`, the shared includes
>    `apps/os88*.inc`, other packages, and every tool that writes shipped
>    bytes (`os88pkg.py`, `os88disk.py`, `os88drv.py`, `os88mini.py`,
>    `buildnum.py`, the compressors) are off-limits. A win there is RECORDED
>    with an estimate (bytes and carriers), not taken. The package's own
>    generator scripts and data sources are yours.
> 5. **SPEC.md first.** The package's section says what changed, in the same
>    commit. The ratchets stay ratchets (`tests/textsites.txt`,
>    `tests/movable.txt`), and `make` (which runs the fast tier) is green at
>    every commit you keep.
> 6. **Testing is host-side** (CLAUDE.md, *Testing*): `make -j2`, `make
>    small` / `make smallapps` when there is a small arm, and pure host
>    checks. **Do not run soak rows or `tests/*.py` scripts, by any route.**
>    One boot to LOOK at the package is allowed (screenshots, cropped and
>    zoomed); a cycle count that DECIDES a design may be taken once and
>    written down. Instead, NAME the rows that cover the package
>    (`grep -n <pkg> tests/suite.py`) - the session runs them all once, at
>    the end.
> 7. **Commit small**: one idea a commit, `<pkg>: apps size pass 2 - <what>
>    (-N file bytes)`, ending with the session's attribution lines. Do not
>    push; the session merges your branch.
>
> **Where disk bytes are, in this tree** (4.6 has the detail): code and
> incompressible data cost about a byte a byte; zero-filled or repetitive
> data costs almost nothing packed, so moving it buys little - **always
> measure the packed file, never infer from the image**. Look at: code size
> (the classic pass - pass 1's report for this package lists what it already
> took); initialised data that is zero or computable (to bss, or built at
> entry); tables a generator emits that the package could build itself;
> art and fonts in a format that packs badly; strings (duplicates, lengths,
> a shared terminator); **parts** (`os88parts.inc` pads each part to a
> sector - Skies saved three clusters a disk by merging ten streams into
> one); and data files beside the package.
>
> **Report back**, in this order: a table of before -> after for file bytes,
> clusters per disk (and copies), image, bss, heap claims and the small arm;
> each change with its saving; hot-path cycle counts you took; launch cost
> if you moved work to entry; the soak rows that cover the package; defects
> found (fixed / not fixed); and **wins you could not take** because they
> are in a shared include, the kernel, a driver or a tool - with bytes and
> carriers - and anything you think is risky. Name your branch.

### 4.4 The session's final pass

Once, after the last agent has been merged, per CLAUDE.md *Testing* (the
owner's rule: soak and `test-full` run ONCE, after the last batch):

1. `make -j4`, `make small`, `make smallapps`, `make KERN_SMALL=1` where a
   small arm moved; the fast tier green.
2. `python3 tools/os88diskuse.py --diff <your cut-point snapshot>`.
3. **One** `python3 tools/os88soak.py start -k <row> -k <row> ...` over the
   deduplicated union of the agents' named rows plus the shared rows a
   package change reaches (`appsmall`, `smallreq`, `movable`, `dispapps`,
   `regapp`, `regwork`, the `lz*` family, the association rows), held and
   reported every 15 minutes (fork rule 4). Re-run only what fails.
4. `make test-full` once.
5. The report (5), the push (3), and the six 360KB images sent (fork
   rule 3).

### 4.5 Package constraints the owner has already set

- **Video**: speed and stability outrank size. The play loop, reader, ring,
  hook, card clock, `vp_adue`, Live's sound feed, the decoder, seek, bank and
  XMS paths are untouchable unless measured not slower AND not less stable,
  and every routine a play runs keeps its base address PARITY (pass 1 set 6;
  the `video-player-size` work merged at `2c6edafc` took a hot and a cold
  pass since). Expect little; say so rather than reach.
- **Skies**: very performance-optimised. Frame, raster, 3D and flight code
  moves only on equal-or-better MartyPC cycle counts.
- **DOS** (`apps/dos`, SPEC.md 96): the box is debugged against a real IBM
  DOS (docs/DOS-DEBUGGING.md); a behaviour change there is invisible from
  one side, so prefer layout and data wins over logic rewrites.
- **The Wire, Task Manager, RLAPPLY**: system apps on every system disk
  (`SYSTEM/`); The Wire's brand table is fixed by the owner (CLAUDE.md,
  docs/WIRE-PLAN.md) and is not reworded.

### 4.6 What packs, and what does not

`kernel/lz.inc` / SPEC.md 20.13: every package is LZ4 with a raw tail. LZ4
finds repeats of four or more bytes within 64KB. So:

- a run of zeros or a repeated record compresses to almost nothing - moving
  a zero table to bss saves IMAGE bytes (RAM-neutral) but few FILE bytes;
- code compresses poorly, ~10-25%; a code byte saved is most of a file byte;
- a table the package could compute (sine, CRC, a projection, a bitmap
  mirrored or shifted) is the best disk win there is: its whole packed size
  goes, for an entry-time loop of a few dozen bytes;
- the same data twice in one image costs ~3 bytes a copy packed, so
  de-duplicating it buys little on disk - measure before you spend a day;
- a part (`os88parts.inc`) is padded to a sector boundary, so N parts waste
  ~256 x N bytes on average.

## 5. The report, and what comes back to the owner

Each set writes `docs/reports/APPS-SIZE-P2-SET<X>-<date>.md` in pass 1's
shape: tree and box, method, **the table** (file before / after, clusters
freed on each disk, image + bss, runtime claims), hot paths, user-visible
changes, defects fixed and not, coverage (the rows run and their result),
and **the wins no set could take** - shared includes, kernel, drivers,
tools - each with bytes, carriers and disks. The last set to land folds
every set's list into one ranked table here (6) for the owner to pick from.

## 6. Results, set by set

| set | report | free clusters gained (system 360 / apps 360 / other) |
|---|---|---|
| A | docs/reports/APPS-SIZE-P2-SETA-2026-10-16.md | system +1, network +1, small360 +3, smallapps360 +3; apps360 +0 (-2,138 B off the system disk's full arms) |
| B | docs/reports/APPS-SIZE-P2-SETB-2026-10-16.md | 0 / **+22** / office +12, games +12, small360 +17, smallapps360 +17, network +4 - the code pass is -3,509 file bytes, the rest is the owner's LZB decision (6.2) |
| C | docs/reports/APPS-SIZE-P2-SETC-2026-10-16.md | 0 / +3 / games360 +3, small360 +3, smallapps360 +3 (-2,610 file bytes across 8 packages; image + bss -2,561) |
| D | docs/reports/APPS-SIZE-P2-SETD-2026-10-16.md | 0 / **+1** / +1 games360, +3 media360 (+5 in all; -5,366 file bytes) |
| E | deferred | - |

### 6.2 What set B changed for EVERY set: nineteen builds ship LZB

**The owner decided, mid-set B, that a package whose disks matter more than
its launch ships LZB** (SPEC.md 20.13.5). The test is that LZB saves it a
1KB cluster on the 360KB disks it rides; Paint, Note Pad and the Task
Manager stay LZ4 whatever it would save them, because they open the common
documents and must stay quick. On the tree set B landed on, that is
ArtfulType, Audio, Browser, Calculator (both arms), Chart, Cyclone, Dot
Delirium, Gorillas, Missile, Sheet, Solitaire, TameGram, Tank (both arms),
Telnet, TeXPad, The Wire and WireBall - sets A, C, D and E's packages among them, by their Makefile rule only. **Arkanoid was
on the list and came OFF at set B's landing**: set C's pass took its LZ4 file
to 5 clusters, the same as LZB's, and `lzbclust` failed on it - which is the
row working. Six more joined after the landing, at the owner's word -
Telnet, Audio, Chart, Gorillas, The Wire and Browser.

- **The list is `LZBPKGS` in the Makefile** and the packer `$(OS88PKGB)`. It
  falls back to LZ4 on a `COMPRESS=lz4` kernel, which carries no LZB decoder.
- **A set that shrinks a listed package must re-run `soak -k lzbclust`**:
  it fails any listed package whose LZB file no longer saves a cluster, and
  `--candidates` lists the LZ4 ones it would now save one for. Measured on
  ArtfulType, LZB costs **+541 ms on a first launch** off a 360KB floppy -
  paid for nothing once the cluster is gone.
- **Measure an LZB package's savings packed LZB**: a code byte saved under
  LZB is worth slightly less file than under LZ4, and an LZ4-shaped
  re-encode can be WORSE under LZB (ArtfulType's splash: -88 under LZ4,
  +121 under LZB, not taken).

### 6.1 What set A learned that the other sets can use

- **A per-package pass rarely crosses a 1KB cluster**: seven of nine packages
  lost 200-700 packed bytes and crossed nothing. The APP_SMALL arms are where
  clusters came back.
- **The file follows UNIQUE bytes**: de-duplicating a repeated idiom often
  ADDS file bytes (LZ4 packs a repeat for ~3 bytes). Measure the packed file
  after every change.
- **The biggest levers are structural and outside every set**: DOS.O88's
  three parts are padded to sectors, and three recorded fixes (a packed
  loader in `tools/os88pkg.py`, the core inline in part 0 in `kerndos/`,
  box-drawing glyphs generated at launch in `apps/os88cp437.inc`) are worth
  about a sector each - ~1.5 KB, a cluster or two on every system disk. The
  set A report's last table is the full list.
