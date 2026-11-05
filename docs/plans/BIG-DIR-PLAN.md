# Big directories — uncapping the Disk window's listing

**OPEN, nothing built.** Studied on `elendilon` at `c797894`, branch
`disk-file-count`. The ask: a Disk window (and the Standard File chooser,
which is the same `KIND_FILES` instance) shows at most `DSK_NENT` entries -
**64 on `kern_big`, 32 on `kern_small`** (SPEC.md 22.6.2) - and should show
**at least 256**, with icons reused or dropped to the stock icon when there are
too many. No permanent RAM growth: a claim that grows while a big folder is
being looked at, and gives the memory back afterwards, is acceptable.
Pagination is the fallback if that proves impossible.

**Verdict: it is possible, and pagination is not needed.** Almost all of the
machinery already exists; what the cap is made of is one constant, a handful
of byte-wide index holders, and a sort that was chosen for 32 entries.

## 1. What the cap actually is today

The listing is **already a per-window heap claim**. LISTING-HOME-PLAN 13
retired the `.lowbss` floor: `fm_kinit` claims `VIEW_KB` (2KB) per Disk
window, `fmv_load` hands it to the mount with `dsk_dest_x`, and the mount
writes straight into it. Its shape (`dsk_dest_x`, disk.inc):

```
offset 0            [dsk_nmax] entries x DSK_DE_STRIDE (24)   1,536 at 64
[dsk_ioff]          one icon-REFERENCE byte per entry            64
                                                              -----
                                                              1,600 of 2,048
```

The cap is `[dsk_nmax]` = `DSK_NENT`, enforced at exactly one place, the scan
in `disk_mount_x`:

```
    inc bp
    cmp bp, [dsk_nmax]
    jae .scan_done              ; the cap: extras are invisible
```

Everything past entry 64 is silently not there: not counted, not reported.

Things that are **NOT** part of the cap, and need no change:

- **File operations.** `dskw_find`, `OSAPI_FILE_FIND` (`dsk_find_x`) and every
  `OSAPI_FILE_*` re-walk the directory on the disk (SPEC.md 19.7.1); no package
  reads the listing (LISTING-HOME-PLAN 2). A 300-file folder already works for
  every program - it is only the window that cannot see it.
- **Icons.** A listing holds one reference BYTE per entry into the machine-wide
  store (SPEC.md 25.9), and a full store already answers `ICO_R_NONE` = the
  generic icon (`ico_put` / `ico_ref_try`, disk.inc). "Reuse icons, or drop to
  stock when there are too many" is **today's behaviour**: identical bodies
  share a row, and the 25th distinct body on a machine whose store cannot grow
  gets the stock icon. Nothing to build.
- **`FS_N`, `FS_SEL`, `FS_SCRL`** are already words (files.inc).
- **`FS_IOFH`** holds the icon base's high byte; 256 x 24 = 0x1800 and
  1,024 x 24 = 0x6000 both fit, provided the base stays 256-aligned (4.2).

## 2. The design: size the claim to the folder, every time

One mechanism for every volume kind, with no new pinned byte:

1. **The scan counts past the cap.** At `[dsk_nmax]` it stops *storing* but
   keeps *counting* into a new word, `[dsk_ntot]` (the folder's true listed
   count). It must now read the rest of the directory, which a full listing
   needed anyway. `[disk_nfiles]` stays the number STORED.
2. **`fmv_load` grows and re-lists.** After the mount returns - and after
   `dsk_dest_x` has retired the destination, which is what keeps
   `tools/dsegaudit.py` clean (a claim may compact; nothing may hold
   `[dsk_dseg]` across one) - if `[dsk_ntot]` > `FS_N`, `mem_regrow` the
   window's claim to `need(ntot)` and mount once more. The second walk of the
   same directory mostly hits the directory-sector cache (SPEC.md 19.2.3), so
   it is CPU, not revolutions. This happens only on the first visit to a
   folder bigger than the claim.
3. **And shrinks to fit.** After every store, `mem_regrow` the claim down to
   `need(FS_N)`. A shrink keeps the base and never moves or fails
   (memory.inc), so it is cheap and safe. A window on a 10-file folder holds
   1KB, not 2KB; a window on a 300-file folder holds 8KB for exactly as long
   as it shows that folder. **That is the "regrowable claim while they are
   looking at the directory"**, and it makes the common case cheaper than
   today's fixed `VIEW_KB`.
4. **A refused grow is a normal path** (CLAUDE.md rule 6, SPEC.md 50.3). The
   window keeps the truncated listing it has, and the status line says so -
   `64 of 300 shown - not enough memory` - instead of today's silence. This is
   the only place pagination-like behaviour survives, and it is a refusal
   with a reason rather than a feature.

`need(n)` = the entries, then the reference bytes, then the selection bitmap
(4.3), rounded up to whole KB:

| entries | entries | refs | bitmap | claim |
|---:|---:|---:|---:|---:|
| 10 | 240 -> base 256 | 10 | 2 | **1KB** |
| 64 | 1,536 | 64 | 8 | **2KB** (today's size) |
| 256 | 6,144 | 256 | 32 | **7KB** |
| 1,024 | 24,576 | 1,024 | 128 | **25KB** |

**`dsk_ioff` becomes derived from the COUNT, not the capacity**: set at
`.scan_done` to `roundup256(n x 24)`, so the reference bytes sit right after
the entries actually stored and the shrink in step 3 never has to move them.
The harvest and the blank already run after the scan, so nothing reads
`[dsk_ioff]` earlier.

**A hard ceiling stays, and it is about index width, not RAM**:
`DSK_NMAX` (renamed from `DSK_NENT`, which stops being a size) - proposed
**1,024 on `kern_big`, 256 on `kern_small`**. Entries past the ceiling are
counted and reported (`1,024 of 1,300 shown`) exactly like a refused grow.
1,024 keeps the claim under 32KB and every `index x 24` product inside 16
bits; a FAT12 root is 112-224 entries and only a subdirectory can exceed it.

## 3. What has to change, by file

The census of everything that assumes a small listing is section 6. In
summary:

### 3.1 `kernel/disk.inc` - the mount

- The scan: store below `[dsk_nmax]`, count always (`[dsk_ntot]`).
- `.scan_done`: derive `[dsk_ioff]` from the count (`dsk_dest_x` stops
  deriving it).
- **The sort.** It is a selection sort, O(n^2) compares: 2,016 at 64,
  **32,640 at 256** and 523,776 at 1,024. At ~150-200 cycles a compare that is
  a **predicted ~1.0-1.4 s at 256 on a 4.77 MHz 8088** and tens of seconds at
  1,024 - not measured; the final pass measures it on MartyPC. It has to
  become O(n log n)-ish: a Shell sort over the 24-byte records (Ciura gaps;
  ~3,000 compares at 256) is the smallest code that gets there, and keeps the
  "folders first, then case-insensitive name" key exactly as it is.
- **The icon harvest.** Each `.o88` whose body is not already in the store
  costs one `int 13h` peek - **~400 ms each**. A folder of 256 unknown
  packages would be ~100 s of seek. Add a **per-mount read budget**
  (`DSK_HARV_MAX`, proposed 64, i.e. today's worst case): once spent, the
  remaining unknown packages take `ICO_R_NONE`, the stock icon - which the
  ask explicitly allows. Folders and store/`ASSOC.DAT` hits cost no read and
  are not budgeted, so a re-visit fills in what the first visit skipped.
- The `%error`s that pin 64 / 254 (section 6) move to the new ceiling.

### 3.2 `kernel/files.inc` - the window

- `fmv_load`: the grow-and-re-list and the shrink-to-fit of section 2;
  `fmv_fit` becomes "claim `need(0)`" for a window with none.
- `fm_kinit`: claim 1KB, not `VIEW_KB` (the first store sizes it).
- `FS_VKB` keeps meaning "KB actually held", now variable.
- **`FS_MSEL` leaves the resident window block** and goes into the claim,
  after the reference bytes (`n/8` bytes, 32 at 256). The pool block shrinks
  by 8 bytes x `FM_NSLOT` (5) = **-40 resident `.bss` bytes on `kern_big`**,
  which pays for part of the work. Every `FS_MSEL` reader (SPEC.md 22.27) takes
  its bitmap through `[fm_vseg]` instead of `DI`.
- `fm_anch` (the Shift anchor) and every other byte that holds an INDEX
  (section 6) become words.
- Status line: `N of M shown` when `[dsk_ntot]` > `FS_N`.
- Scroll bar / icon-grid arithmetic re-checked for 16-bit products at 1,024
  rows (section 6).

### 3.3 `kernel/loader.inc`, and the other index holders

`[ld_pending]` is a BYTE holding index + 1 and has an `%error` at
`DSK_NENT > 254`: it becomes a word. The rest of section 6's byte-wide holders
likewise.

### 3.4 Host side

- `tools/os88disk.py` refuses a disk with more listed entries in a folder than
  the kernel can show, reading `DSK_NENT` out of `kernel/dskwin.inc`. It reads
  the new ceiling instead (`DSK_NMAX`), both arms, and `tests/unit/t_mirror.py`
  follows. `tools/os88httpdrv.py` reads the same constant.
- No shipped disk changes content from this (none has a folder over 64 today),
  apart from the kernel itself.

### 3.5 SPEC.md

- 22.6.2 (`DSK_NENT` is 64/32) is superseded by a new subsection: the listing
  is sized to the folder, the ceiling and why it is a width and not a budget,
  the refusal text, the harvest budget.
- 22.1 (`VIEW_KB` per window) and 22.27 (`FS_MSEL` is 8 bytes in the block).
- 19.4 (the sort).

## 4. Constraints the design is shaped by

### 4.1 `dsegaudit`: no claim while the destination is live

`[dsk_dseg]` may name the window's claim only across the mount, and nothing in
that span may reach `mem_claim` (compaction moves the block - SPEC.md
66.5.10.2). That is why the grow happens **between** two mounts in
`fmv_load`, never inside the scan when it fills up. Growing inside the scan
would be one walk instead of two and is refused for this reason.

### 4.2 `FS_IOFH` is one byte

`fmv_iofs` and the cache's icon base are carried as a high byte, so the
reference index must start on a 256-byte boundary. `roundup256(n x 24)` keeps
that, at a cost of up to 255 bytes of padding per claim.

### 4.3 Resident bytes

Expected: + the count-past-cap, + grow/shrink in `fmv_load`, + the status-line
variant, + the harvest budget, +/- the sort rewrite (a Shell sort is a little
larger than the selection sort), - 40 `.bss` (`FS_MSEL` moving out), and the
byte-to-word widenings. Headroom at `c797894`, from `make`'s own `kernsize` lines: `kern_big`
KERN_SIZE 96,768 of KERN_BUDGET 100,864 (4,096 spare); `kern_small` 59,392 of
64,000 (4,608 spare) - stated in bytes as the banner rule asks, and the work
is priced against the byte, not those steps.
A guess is a few hundred bytes of `.cold` on
`kern_big`, resident and billed by `KERN_BUDGET`; to be **measured by span**,
not by rung, on both kernels before the first commit that spends it. On
`kern_small` the multi-select half does not exist and the rest is smaller.
If it does not fit `kern_small`'s 64KB rule, `kern_small` keeps 32-entry
`DSK_NMAX` = its claim-sized listing without the sort rewrite - a per-build
decision for the owner, not a quiet cut.

### 4.4 `kern_small`'s claim is PURGEABLE

`fmv_demote` already handles a shed listing (`FSD_CACHE` debt, re-list on
focus). A bigger claim is a likelier shed target; that is the existing
contract working, not a new failure mode. 256 x 24 = 7KB on a 128KB machine
is the reason its ceiling is proposed lower.

## 5. Waves

1. **Count and report.** `[dsk_ntot]`, the `N of M shown` status line. No cap
   raised yet: the first user-visible fix is that a truncated folder SAYS so.
2. **Widen the index holders** (section 6.1), size the copy list to the
   selection (6.2), and move `FS_MSEL` into the claim, at the old cap - every change behaviour-neutral, so any regression is the
   widening's.
3. **The sort** (Shell sort), measured on MartyPC at 64 and at a synthetic
   256 before it lands.
4. **The grow-and-shrink claim**, `DSK_NMAX` ceilings, the harvest budget,
   `os88disk.py` and `t_mirror`.
5. **SPEC.md and the row.** A new soak row on a scratch disk with a 300-entry
   folder (built by `os88disk.py`): open it, read `FS_N` = 300 and the claim
   size off the guest, scroll to the last row and click it, Ctrl+A,
   navigate up and watch the claim shrink back, close and watch it freed.
   Break it on purpose (put the old cap back) and watch it go red
   (docs/WRITING-TESTS.md).

Existing rows the waves can reach, to run once as the deduplicated union after
the last wave (names from `tests/suite.py`): the Disk window's `fm`,
`fmarrows`, `fmbtn`, `fmcommit`, `fmpatch`, `fmrefuse`, `fmthumb`, `msel`,
`shedrelist`, `paintdirty`; the chooser's `fdlgchoose`, `fdlgchsmall`,
`fdlgdrop`, `fdlggrey`, `fdlgsmall`, `fdlgup`; the listing and icon rows
`hdnoclaim`, `rdmount`, `rdicon`, `icogrow`, `icostore`, `icohd`,
`ascabsorb`, `dirwshed`; the gates `dsegaudit`, `mirror`, `lowwin`; and
`small128`. Plus `test-full` once.

## 6. Census: everything that assumes a small listing

From a read-only audit of the tree at `c797894`, with the sharpest items
re-checked by hand. The headline: the scan, the sort, the harvest, every
`index x stride` product and all of the scroll/grid/thumb arithmetic are
already WORD math and are good to 2,730 entries. What breaks is a handful of
byte holders - **several of them at 128, not 256, because of `cbw`** - one
fixed-size bitmap, one fixed-size copy list, and the guards and tests that pin
the number.

### 6.1 An index or a count in a byte

| where | what | breaks at |
|---|---|---|
| loader.inc:2072 `ld_pending: resb 1` | index + 1; written files.inc:4340, read ui.inc:910, cleared disk.inc:4065 | index 255 -> 0 = "none" |
| files.inc:4343 `mov [ui_post], al` | index + 1, commented "never 0" | index 255: the post is lost |
| files.inc:9366 `fm_anch: resb 1` | the Shift anchor, 0FFh = none; written 3599, 3641, 8285, 8320 | 255 collides with "none" |
| files.inc:8316-8322 (`fm_mto`) | `cmp al, [di+FS_N]` compares FS_N's LOW byte, then `cbw` | **128** (sign extension) |
| files.inc:8397 `fm_mw_cnt` | counts the delete set in DL; read 5053 (`or dl,dl`), 5056-5057 (`cbw`, "< 64") | **128**, wraps at 256 |
| files.inc:232 | FS_N is a word; only its comment ("0..32") is stale | - |

Checked and clean: `fm_dgidx`, desksc.inc:711, `FS_SEL`/`fm_lsel`,
fdlg.inc:876/997, compress.inc:1458, loader.inc:683, hiber.inc:4671,
assoc.inc (documents post by name), stikmenu.inc, diskops.inc.

### 6.2 Fixed-size side tables

- **`FS_MSEL`**, files.inc:377-386: 8 bytes, `%error` past 64. `fm_many`
  (8212-8215) and `fm_mzero` (8224-8227) are unrolled to exactly four words;
  `fm_mbit` (8196-8204) is general. Moves into the claim (3.2).
- **`fm_pool`**, files.inc:9360-9376: sized by literals (`5*69`, `5*61`,
  `5*24`), which follow `FS_SIZE` when `FS_MSEL` leaves the block.
- **The copy list**, files.inc:5800-5812: Cut/Copy of a multi-selection claims
  a fixed **1KB = 64 records of 16 bytes** (`MEM_P_FLIST`), and `fm_mw_rec`
  adds 16 to `[fcp_lcnt]` with no bound - **a selection of more than 64
  entries would overrun the claim**. It becomes `need = count x 16`, rounded
  to KB, counted with `fm_mw_cnt` (once that is a word) before the claim.
- `DSK_ICOIX_N` (dskwin.inc:190, copied at files.inc:1165), `VIEW_KB`
  (kernel.asm:1497, `fmv_fit` at files.inc:1215-1223), `FV_ICONS`
  (files.inc:401) - all become count-derived (section 2).

### 6.3 Arithmetic

- 16-bit `mul` for `index x 24` everywhere (disk.inc:4300, 8218-8221, 9110;
  files.inc:783-785, 830-832): good to 2,730.
- `FS_IOFH` (files.inc:263, 807-808): the 256-alignment, handled by
  `roundup256` in section 2 rather than by restricting the count to multiples
  of 32.
- The 8-bit `mul`/`div` at files.inc:1251/1299, fdlg.inc:445/1026 and
  diskops.inc:3327 are on window slots and volume rows, not entries: safe.
- The O(n^2) sort (disk.inc:4313-4330): speed, not overflow (3.1).

### 6.4 Scroll bar, rows, grid

All word math - `fm_layout` (files.inc:2957-2961), `fm_div` (9124-9126), the
`mul word`s at 3846/4153/7783, `fm_sbset` (3217-3222) and the shared thumb
(apps/os88ui.inc:2163-2207). Nothing overflows at 1,024 rows. fdlg.inc has no
row math of its own.

### 6.5 The icon store

Full store -> `ICO_R_NONE`, cleanly (`ico_put`, disk.inc:9877-9890), whether
the cause is no store, `ICO_KBMAX` or a refused `mem_regrow`. The store grows
only on `kern_big` (`OS88_ASSOC`: 5KB to 20KB, at most 247 rows); `kern_small`
has a fixed 24. **One consequence worth a sentence in the SPEC**: a 1,024-entry
folder of DISTINCT packages can fill the machine-wide store, after which other
volumes get generic icons until the store sheds. That is the existing
contract, and the harvest budget (3.1) limits how fast one folder can do it.

### 6.6 Guards, tools, tests

- Definitions: dskwin.inc:234/236. Guards: loader.inc:2079 (`> 254`),
  files.inc:385 (`> 64`), 429 (`<= VIEW_KB`), 807 (multiple of 256),
  disk.inc:9269 (icon rows - unaffected).
- Stale comments: disk.inc:4302, 4705, 8221; files.inc:5776; loader.inc:2078.
- `tools/os88disk.py:85-117` (+ help text at 2021) and
  `tools/os88httpdrv.py:72-84` parse `DSK_NENT`; `tests/unit/t_mirror.py`
  checks them.
- `tests/msel.py:62-63` reads exactly 8 bytes and `range(64)` - follows
  `FS_MSEL` into the claim.
- `tests/rdmount.py:12, 66` ties a blank size to `[dsk_nmax]` = 64 - follows
  the count-derived blank.
- `hdnoclaim`, `icogrow`, `icostore`, `ascabsorb`, `icohd` read `[dsk_nmax]`
  off the guest and work at any size.

### 6.7 The Standard File chooser

It has **no store of its own**: it is the fifth `fm_pool` block
(`FM_NSLOT = VIEW_SLOTS + 1`), seeded by `fdlg_open` and listed by the same
`fm_kinit` / `fmv_fit` / `fmv_load`. So everything in section 2 covers it,
`FDLG.DRV` on `kern_small` included. `MEM_K_FDLG` (memory.inc:254-265) is
reserved and unused (SPEC.md 38.1).
