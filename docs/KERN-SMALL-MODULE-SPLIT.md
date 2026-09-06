# The module split, `kern_small` only

**Research document, not a contract.** SPEC.md §2.8 is the binding contract for
what an on-demand module *is*; docs/ONDEMAND-PLAN.md is why the three that ship
were chosen; this is the study of moving more of `.cold` behind that mechanism
**on `kern_small` alone**, with `kern_big` keeping every byte resident. Nothing
here has been built. Every figure was measured on this tree at build 377 by the
method in §8.

The ask, in the requester's words:

> Investigate what it would take to do the module split, for kern_small ONLY.
> Big needs those to stay fast, and to keep loading via the cyl run kernel read
> on boot.

---

## 0. The verdict, up front

**Two of the four candidates are possible and two are refused by the mechanism
itself. The net is ~4.6 KB, not the ~13.0 KB docs/KERN-SMALL-CUT-PLAN.md §6
claimed** — that figure was the four files' `.cold` added up, and this
investigation is what checked it.

| candidate | `.cold` | entries | verdict |
|---|---:|---:|---|
| `filecp.inc` — Cut/Copy/Paste | 2,141 | **5** | **possible**, and clean |
| `fdlg.inc` — Standard File dialog | 3,152 | **9** | **possible**, after two prerequisites |
| `assoc.inc` — file associations | 2,003 | 9 | **refused — a cycle**, §2.1 |
| `diskw.inc` — the FAT write path | 4,565 | **33** | **refused — it is the file I/O layer**, §3.4 |

Five findings:

1. **`kern_big` is untouched by construction, and its boot read gets no
   slower.** A module is cut out of `kernel.bin` by `tools/os88mod.py` and
   `MODC_START` is exactly where the image ends, so modules already ride
   *outside* the contiguous cylinder-run read. Gating the split on
   `%ifdef KERN_SMALL` leaves big's `.cold`, its sector count and its run
   identical — and makes `kern_small`'s boot read **shorter**, which is a
   small win in the other direction.

2. **The module loader's own dependency cone is half of `.cold` and can never
   be modular.** `mod_need`'s transitive callees are **155 symbols across 7
   files — 16,880 bytes, 48.9% of `.cold`**. Add `files.inc` (the Disk window,
   always live, 55 entry points) and 71% of `.cold` is structurally excluded
   before any judgement about frequency. §2.

3. **`assoc.inc` is *inside* that cone**, which settles it on mechanism rather
   than on the disk-swap test:
   `mod_need → drv_mounted → dsk_chdir_q_x → dsk_chdir_x → disk_mount_x →
   asc_lookup_x`. Loading any module can trigger a mount, and a mount calls
   associations. §2.1.

4. **`diskw.inc` is not "the write path", it is the by-name file I/O layer**,
   and three loaders plus two shipped modules depend on it: `mod.inc` calls
   `dskw_read_x` *to load a module*, `driver.inc` calls it to load a driver,
   `loader.inc` calls `dskw_stat_x` to load a package, and CTRL.DRV and
   CLONE.DRV far-call `dwf_dskw_*` from inside their own images. 33 entry
   points against `MOD_NENT`'s 8. §3.4.

5. **`fdlg.inc`'s 4,292 bytes include ~1,140 that are not fdlg's.**
   `apps/os88ui.inc` — the shared standard controls — is `%include`d inside
   fdlg's `.cold` extent, and its kernel copy is used by five other files
   (`os88ui_btn` in five, `os88ui_arm`/`_armed`/`_fire`/`_krect` in three
   each). It has to be lifted out first, **and it stays resident whether fdlg
   is moved or deleted** — so KERN-SMALL-CUT-PLAN §4's C2 is overstated by the
   same 1,140. §6.

---

## 1. What a module costs, measured off the one that ships

`diskw.inc`'s `section .modf` block (line 3718) is the worked example, and the
shape is worth stating because every cost below comes off it:

```nasm
section .modf                   ; the on-demand image (SPEC.md 2.8)
modf_hdr:
    dw 0x384F                   ; MOD_H_MAGIC
    db MOD_VER
    db MOD_FMT                  ; which MOD_* row this is
    dw BUILD_NUM                ; the commit...
    dw MOD_STAMP                ; ...and this build's LAYOUT
    dw modf_end                 ; MOD_H_IMG
    dw FM_NENT
    dw modf_e_probe             ; the FAR entries, never the bodies
    ...
    times MOD_NENT - FM_NENT dw 0

modf_e_probe:   call dskw_fmt_probe_x
            retf
```

**A module is a section, not a file.** `diskw.inc` already contributes to
both `.cold` and `.modf` — the formatter is split out of that file and the
rest of it stays resident. So the unit of the split is a *named subset of
routines*, which is what makes a `%ifdef KERN_SMALL` version of it possible at
all.

The cost, per module:

| | where | bytes |
|---|---|---|
| image header | in the module | `12 + MOD_NENT*2` |
| one far entry per exported body (`call body_x` / `retf`) | in the module | 4 each |
| the shared `..._load` stub (`push ax` / `mov al, MOD_x` / `call mod_need` / `pop ax` / `ret`) | resident `.cold` | ~10 |
| one resident stub per entry — load, `call far [FP + K*4]`, and **its own refusal** | resident `.cold` | ~14 each |
| one far entry into `.cold` per outbound symbol with no existing `.text` thunk | resident `.cold` | 4 each |
| `mod_tab` row + the 8.3 file name | resident `.text` | ~16 |
| `mod_fp` | resident `.bss` | `MOD_MAX * MOD_NENT * 4` |

**Outbound is cheap and inbound is what binds.** `COLD_SEG` is an
assembly-time constant, so module code reaches resident cold code with a plain
`call COLD_SEG:xxxf_foo` — the convention already exists at scale, **80
`xxxf_` far entries** in the tree today (`drvf_` 16, `dwf_` 12, `mmf_` 11,
`cpf_` 10). Calls out to `.text` need nothing new at all: the module is cold
code by every other property, so the 102 `cw_*` shims already serve it.

**`MOD_NENT` is 8 and that is the ceiling on entry points.** It is not a free
constant: `mod_fpr` turns a row pointer into `&mod_fp[id]` with three unrolled
`shl di,1`, and `kernel/mod.inc` asserts `MODFP_STRIDE == MODR_SIZE * 8`.
— **superseded**: it landed at **7**, with a ×7 = ×8 − ×1 chain; §9.2 has why.
Raising it to 16 is a four-line change — one more shift, the assertion's `8` to
`16` — and doubles `mod_fp`.

---

## 2. The ceiling: `mod_need`'s own cone

Walking every `.cold` call reachable from `mod_need`, `mod_check`,
`mod_free_row`, `mod_init_x` and `mod_drop`:

| file | symbols in the cone | that file's whole `.cold` |
|---|---:|---:|
| `disk.inc` | 61 | 5,746 |
| `memory.inc` | 26 | 2,388 |
| `assoc.inc` | 24 | 2,003 |
| `diskw.inc` | 20 | 4,565 |
| `driver.inc` | 11 | 1,794 |
| `mod.inc` | 8 | 366 |
| `kernel.asm` | 5 | 18 |
| | **155** | **16,880 — 48.9% of `.cold`** |

None of it can be a module: the loader would have to load itself. Add
`files.inc` (7,653, and 55 entry points — it is the Disk window, live for the
whole session) and **71% of `.cold` is excluded on structure alone**.

What is left, ranked by whether it fits `MOD_NENT` = 8 today:

| `.cold` | file | entries | out → `.cold` | verdict |
|---:|---|---:|---:|---|
| 3,173 | `fdlg.inc` | 11 names / **9 bodies** | 21 | needs `MOD_NENT` 16 |
| 2,141 | `filecp.inc` | **5** | 33 | **fits today** |
| 1,111 | `apps.inc` | 11 | 6 | the built-in kinds — multi-instance, §7.3 |
| 995 | `desk.inc` | 8 | 7 | the desktop — drawn constantly, refuse |
| 781 | `loader.inc` | 5 | 12 | the package loader — §7.3 |

### 2.1 Why `assoc.inc` is a cycle and not a judgement call

docs/ONDEMAND-PLAN.md §1's test would already refuse it: `asc_lookup_x` and
`asc_take_x` are called **once per directory entry** inside `disk_mount_x`'s
icon-harvest loop, so a module load would sit in the mount's inner loop.

But it does not get as far as the test. The path is:

```
mod_need -> drv_mounted -> dsk_chdir_q_x -> dsk_chdir_x -> disk_mount_x -> asc_lookup_x
```

`mod_need` banks to the system volume, and reaching it can mount it. **Loading
the association module would call the association module.** Refused.

---

## 3. The four candidates

### 3.1 `filecp.inc` — Cut/Copy/Paste. The clean one.

Five entry points, and all of them a user gesture: `fcp_arm`, `fcp_ncopy`,
`fcp_paste`, `fcp_answer` from `files.inc`, and `fcpf_fcp_goto` — **already a
far entry**, called by CLONE.DRV from inside `.modl` and by `kernel.asm`.

Outbound is 33 symbols into other `.cold`, of which 9 already have a resident
`.text` thunk to far-call through and **24 need a new far entry** — 14 in
`disk.inc`, 6 in `diskw.inc`, 2 in `memory.inc`, plus `drv_fs_call`/`drv_fs_has`
and two `kretc_*`. At 4 bytes each that is 96 resident bytes.

The drop point is unambiguous: a copy or paste is one bounded operation with a
progress widget already on it, so `mod_drop` goes where `fpg_` comes down.

### 3.2 `fdlg.inc` — the Standard File dialog. Possible, with two prerequisites.

**Nine distinct bodies**, from eleven names — `fdf_fdlg_open` and `fdlg_open_x`
are the same body reached two ways, as are `fdf_fdlg_paint` and
`fdlg_paint_x`. The set is `open`, `paint`, `onkey`, `onclick`, `ondrag`,
`onup`, `reap`, `grab`, `top`.

**The window callbacks are already solved.** `fdlg_tpl` in `.text` holds
`dw fdlg_s_topen, fdlg_paint, fdlg_onkey, fdlg_onclick` — *thunk* names, not
cold bodies, and the thunk far-calls `fdlg_paint_x`. Turning a thunk into
"`mod_need`, then `call far [FDFP + K*4]`" leaves the template untouched and
`wm_create` none the wiser.

Two prerequisites:

- **Lift `%include "os88ui.inc"` out of `fdlg.inc`** (line 1211) into a
  resident `.cold` position of its own. Mind `kernel.asm`'s ordering trap: the
  `OS88UI_SBDRAG` define must be resolved before the include, and the file
  already carries the account of what happened when it was not.
- **Raise `MOD_NENT` to 16**, per §1.

The lifetime rule is the Control Panel's: the window may only exist while the
module does. `cp_open_x` refuses *before* `wm_create`, and `fdlg` must do the
same, or a repaint arrives with nothing to call.

### 3.3 `assoc.inc` — refused, §2.1.

### 3.4 `diskw.inc` — refused. It is the file I/O layer.

Thirty-three entry points against a ceiling of eight, and the entry list says
why the name misleads:

- `mod.inc` calls **`dskw_read_x`** — this is how a module is read off the
  disk. A module containing it could not be loaded.
- `driver.inc` calls `dskw_read_x` and `dskw_stat_x` to load a `.DRV`.
- `loader.inc` calls `dskw_stat_x` to load a **package**.
- `disk.inc` calls `dskw_flush_x` and `dskw_remount_x`.
- **CTRL.DRV** (`.modc`) far-calls `dwf_dskw_read`, `dwf_dskw_stat`,
  `dwf_dskw_write_sys`; **CLONE.DRV** (`.modl`) far-calls four more.
- `kernel.asm` publishes eleven `dwf_dskw_*` far entries as API slots.

A *subset* — the gesture-driven writes (`delete`, `mkdir`, `rename`, `rmtree`,
`ent_store`, `take_slot`, `append`, `char`) — is conceivable, but `filecp.inc`
calls ten of them, so doing both waves would need module-to-module calls. That
is more mechanism than the remaining bytes are worth; §7.2.

---

## 4. What it would take

Per wave, in dependency order.

**`kernel/mod.inc`**
- `MOD_MAX` 3 → 4 (wave 1) → 5 (wave 2), inside `%ifdef KERN_SMALL`.
- New ids `MOD_FILECP` / `MOD_FDLG`, two `mod_f_*` name strings, two `mod_tab`
  rows — all gated.
- Wave 2 only (**superseded**: it landed at 7, not 16 — §9.2): `MOD_NENT` 8 → 16, `mod_fpr`'s three shifts → four, and the
  `MODFP_STRIDE != MODR_SIZE * 8` assertion → 16. **All three move together**;
  the file's own header records what happened last time two of them drifted.

**`kernel/kernel.asm`**
- `section .modp` / `.modd` declarations with `start=` / `vstart=0`, the
  `MODx_START` chain, the `MODx_SIZE` end labels, the `MOD_MAX_KB` guards and
  the `.modmap` rows — every one inside `%ifdef KERN_SMALL`, and the chain must
  reconverge so `MODMAP_START` is right on both builds.

**`kernel/filecp.inc`, `kernel/fdlg.inc`**
- The invasive part: each file carries **both shapes**, `%ifdef KERN_SMALL`
  emitting `section .modX` plus header, far entries and resident stubs, `%else`
  emitting today's `section .cold`. The bodies themselves do not change — they
  keep their near `ret`.
- 43 new `xxxf_` far entries into `.cold` (24 for filecp, 19 for fdlg), also
  gated, or `kern_big` pays for them too.

**`kernel/fdlg.inc`** — lift the `os88ui.inc` include out first (§3.2).

**`Makefile`**
- `KMODS` and `KMODARGS` conditional on `KERN_SMALL`. `os88mod.py` needs no
  change — it reads the row count out of the `.modmap` trailer — but it
  **fails loudly** if the `-m` count disagrees, which is the right failure.
- Disk placement is automatic: `DRIVERS += $(KMODS)` and `SMALLDRIVERS`
  filters that list, so a new module reaches the small floppies with no recipe
  edit. **Check the 360KB cluster budget** — two more files in the root.

**`tools/os88ovlchk.py`** — `MODS = ('.modc', '.modf', '.modl')` is a hardcoded
tuple. Adding sections without adding them here leaves the near-call check
blind to them, which is a failure this gate has already had once (its own
comment records `.modl` shipping that way).

**`tests/suite.py`** — a row that builds `kern_small` and asserts the module
count and each image's size against `MOD_MAX_KB`. `make test-full`'s build
matrix is the only thing that builds `kern_small` at all.

---

## 5. The arithmetic

| | `.cold` moved | resident added | net |
|---|---:|---:|---:|
| wave 1 — `filecp.inc` | 2,141 | 192 | **1,949** |
| wave 2 — `fdlg.inc` | 3,152 | 228 | **2,924** |
| `mod_fp` (`MOD_MAX` 3→5, `MOD_NENT` 8→16) | — | 224 | **−224** |
| | **5,293** | **644** | **4,649** |

Resident added, per wave: wave 1 is 10 + 5×14 stubs, 24×4 far entries, 16 of
`.text`; wave 2 is 10 + 9×14, 19×4, 16.

As the rungs fall on this tree:

```
.cold    34,531 - 5,293 + 388  =  29,626  ->  rung 29,696   (was 34,816)
image    .text +32, .bss +224  =  46,382  ->  rung 46,592   (UNCHANGED)
KERN_SIZE                96,256  ->  91,136
heap floor                95.5 KB  ->  90.5 KB
free heap on 128KB        32.5 KB  ->  37.5 KB
```

**Quote the 4,649**, not the 5,120 the rungs happen to give: the image rung
absorbing 256 bytes of `.bss` for free is this change standing in the right
place, not value it created (CLAUDE.md's rung rule).

---

## 6. What this corrects in docs/KERN-SMALL-CUT-PLAN.md

That document's §6 is wrong and this is the correction, made in place there
too:

- **§6's 12,997 was the four files' `.cold` summed**, with no check on entry
  counts, on the loader's own dependency cone, or on what `%include` sits
  inside `fdlg.inc`. The module route yields **4,649**.
- **§4's C2** (delete the Standard File dialog, 4,654) is overstated by the
  ~1,140 bytes of `apps/os88ui.inc`, which five other files need and which
  survives either route. C2 is **3,514**, and the C subtotal 17,840 → 16,700.
- **§8.1's recommended row moves from 61.4 KB to 53.2 KB**, and the argument
  in §6 that the module route lands "within 1,364 bytes" of deleting C1–C4
  does not survive: the honest gap is **8.4 KB**, because `diskw.inc` and
  `assoc.inc` can be deleted and cannot be moved.

**The recommendation therefore changes.** The module split is still worth
doing — 5.0 KB of heap, +15% on a 128KB machine, with both features intact —
but it is no longer a substitute for the deletions, and §8.1's last row is the
only one that reaches 65 KB.

---

## 7. Refusals and risks

### 7.1 The disk-swap cost is real and this does not dodge it

`mod_need` calls `drv_vol_bank` → `drv_mounted`, so the system disk must be
reachable when the feature is asked for. On the calibration machine — one
360KB drive — opening the file dialog to browse a **data** disk means the
system disk is not in the drive, `drv_mounted` fails, and the dialog refuses.
That is docs/ONDEMAND-PLAN.md §1's objection exactly, and it is the reason
this is a product decision and not a build fix. It is not fatal — the refusal
is clean and the toast can say what to do — but *"the file dialog sometimes
will not open"* is the feature being bought.

### 7.2 Do not build module-to-module calls

`filecp.inc` calls ten `diskw.inc` bodies. If a `diskw` write subset ever
became a module too, one module would have to `mod_need` another from inside
its own image. It is mechanically possible — `mod_fp` is `.bss` at
`KERNEL_SEG`, reachable through DS — and it is a lifetime problem nobody
should have: two claims, two drop points, and a compaction between them.
**One level of modules.**

### 7.3 Three that were measured and are not proposed

- **`apps.inc`** (1,111, 11 entries) — the built-in kinds are **multi-instance**
  windows. `mod_drop` is caller-decided and has no refcount, so the second
  Timer closing would free the first one's code. It needs a pin, which is
  ONDEMAND-PLAN §7.1's purgeable design, deliberately not built.
- **`desk.inc`** (995, 8 entries) — the desktop is drawn on every repaint.
- **`loader.inc`** (781, 5 entries) — the package loader, and `memory.inc`
  calls into it during compaction.

### 7.4 What stays resident either way

`.bss` does not move: `fdlg` 121 and `filecp` 144 stay, and so does every
`.text` byte of both files. The saving is `.cold` alone.

---

## 8. How these figures were taken

Per-file `.cold` from `tools/kernsize.py --modules --build build/smallk
-DKERN_SMALL`. Sub-file sizes from nasm's `[map all]`, each symbol sized by the
distance to the next in its section — the summed spans equal the section
lengths exactly, and nasm-local labels are attributed to their parent.

The call graph is a source scan of `kernel/*.inc` and `kernel/*.asm`: section
state tracked per line, labels owned by the section they are defined in, and
`call`/`jmp`/`dw` targets resolved against that. **One correction is worth
recording**: the first pass missed `call far COLD_SEG:label`, matching the
segment rather than the label, which understated every inbound count — fdlg
read 2 entries instead of 11 and `diskw` 21 instead of 33. A cross-segment
call graph that cannot see segment-prefixed calls is measuring the wrong thing,
and the numbers it gives are plausible.

The 1,140 bytes attributed to `apps/os88ui.inc` are the `.cold` the map
accounts for and the kernel source does not define — `fdlg.inc`'s
`%include` is the only one of its kind in a `.cold` block, which
`tools/os88ovlchk.py`'s own `EXTRA` table independently confirms.

**Nothing here has been built.** Every figure is what the code costs today.

---

## 9. Decisions taken, and the order to build in

The fork owner has settled three things, and one of them changes the order
this document would otherwise have proposed.

| | decision |
|---|---|
| `assoc.inc` | **gated out of `kern_small` entirely** — not a module. *"A nice to have, not something they need to use the system."* |
| `filecp.inc`, `fdlg.inc` | **become on-demand modules**, as §3.1 and §3.2 |
| `diskw.inc` | **kept for now.** Killing file writing outright is still on the table and is a separate conversation — *"a much more core feature for an OS to have"* |

### 9.1 Gating `assoc` is worth more than KERN-SMALL-CUT-PLAN §4's row says

That row prices it at 2,526 bytes of footprint. It misses a second, larger
term: **`asc_use_x` claims 3KB of heap and never gives it back.**

```nasm
ASC_KB       equ 3              ; 32*80 + 16 + 24*4 = 2,672 bytes
...
    cmp word [asc_seg], 0
    jne .haveseg                ; claimed ONCE, for the session
    mov ax, ASC_KB
    mov bx, MEM_K_ASC
    call mem_claim_x
```

`MEM_K_ASC` is a kernel tag (0xFF06), **not one of the `MEM_P_*` purgeable
classes**, and nothing frees it — a volume switch reloads the block in place.
`disk_mount_x` calls `asc_use_x`, and the boot mount is a mount, so **the claim
is standing on a bare desktop**. Gating the feature returns it.

So the honest figure for a 128KB machine is **2,560 of footprint (as the rungs
fall) plus 3,072 of heap = 5.5 KB**, which makes it the single largest item
now on the table — larger than either module wave — and the only one that
needs no new mechanism at all.

**And it means today's headline is optimistic.** `32.5 KB` is what the ladder
leaves; the association cache is holding 3KB of it before the user has done
anything, so the real figure at a bare desktop is **~29.5 KB**. Only this one
claim has been audited — `tests/kernresident.py` walks `mem_tab` at a desktop
and has never been pointed at `kern_small`. **Doing that walk is worth more
than any single row in these documents**, because a pinned boot claim is heap
the machine never gets back and no assembler can see it.

### 9.2 The order, and why it is not the one proposed

The owner's instinct was to start with the two modules. On the measurements
above the order should be **assoc first**, for two reasons that have nothing to
do with its size: it needs **no new mechanism** — no section, no `MOD_*` id, no
`os88mod.py` argument, no `os88ovlchk.py` edit, no `MOD_NENT` change — and it is
the only one of the three whose call sites are already known and few (five in
`disk.inc`'s mount and harvest path, one in `files.inc`, one in `ui.inc`, and
two API-slot thunks in `kernel.asm`).

**The baseline moved under this table.** `elendilon` merged kernel size pass 3
and APP_SMALL, taking `kern_small` from `KERN_SIZE` 96,256 to **94,720** and the
heap floor from 95.5 KB to 94.0. Every row below is against that tree, and W0's
figures are **measured** rather than projected:

| wave | change | mechanism needed | `KERN_SIZE` | free heap | usable |
|---|---|---|---:|---:|---:|
| — | post-merge baseline | | 94,720 | 34.0 KB | **~31.0 KB** |
| **W0** | `assoc` gated out — **BUILT** | none — `%ifdef` | **92,160** | **36.5 KB** | **36.5 KB** |
| **W1** | `filecp` → module — **BUILT** | fits `MOD_NENT` = 8 today | **90,624** | **38.0 KB** | **38.0 KB** |
| **W2** | `fdlg` → module — **BUILT** | lift `os88ui.inc`; `MOD_NENT` → 7 | **88,064** | **40.5 KB** | **40.5 KB** |

**8,192 bytes of footprint over three waves, and ~9.5 KB of usable heap** —
31.0 → 40.5 — because W0 returns a pinned claim as well as two rungs. W2
landed 512 bytes above its projection and needed `MOD_NENT` = 7 rather than
16; §9.2.6 has why.

**All three waves are BUILT, and there is no fourth in this document.** What
is left of the 128KB ask lives in docs/KERN-SMALL-CUT-PLAN.md §8.1, whose
tiers are the next thing to choose from.

### 9.2.1 W0 as built

`KERN_SIZE` **94,720 → 92,160, −2,560**, which is the prediction to the byte:
`.text` −502, `.bss` −43, `.cold` −2,076 (the extra over `assoc.inc`'s own
2,520 is the call sites the gate takes with it). Two rungs uncrossed, the image
and the cold. **`kern_big` is byte-identical** — `kernsize[big]` reports `+0` on
every section.

Verified on the glass under MartyPC, on a 5150 with a CGA:

- `tests/smallboot.py` passes on all three of its machines (CGA, Hercules, and
  a VGA the small build has no renderer for).
- The Disk window mounts, lists and computes free space: *"Drive A: 3 files"*,
  `MEDIA` / `README.TXT 16334` / `SYSTEM`, *"Size 15K Free 179K"*.
- **`README.TXT` draws the generic icon**, against `kern_big`'s composed
  page-with-glyph on the same file — §54.1's all-zero sentinel, which is what
  §54.0 says the machine gets. Nothing else in the row differs.
- Zero `assoc_*`/`asc_*` symbols survive in `kern_small`'s map, and the FAT-form
  string `ASSOC   DAT` is present in `kern_big`'s image and absent from
  `kern_small`'s.

**A cross-kernel pixel diff of the two A: listings is NOT evidence and was
discarded**: the two system disks carry different files (big's A: lists six,
small's three), so the 2,358 differing pixels it reported are the disks and not
the kernels. The icon crop above is the comparison that holds.

### 9.2.2 The gate goes INSIDE the file, and `kernsize.py` is why

The obvious placement — `%ifdef OS88_ASSOC` around `kernel.asm`'s
`%include "assoc.inc"` — **breaks the per-module size report**, and it took a
failed `--bless` to find out:

```
kernsize: per-module pass: the instrumented build failed:
  kernel.asm:8119: error: symbol `KSM27_boot2' not defined before use
```

`tools/kernsize.py`'s `instrument()` inserts its bracketing markers **at the
`%include` line, in Python, over `kernel.asm`'s raw text**, and it does not
track `%ifdef`. A gate around the include therefore puts the *opening* marker
inside it and the *closing* one outside, so on a `kern_small` build the opening
marker is never emitted and the `%assign` that differences them names a symbol
that does not exist.

So the file is **included unconditionally and gated inside**, which is
`band.inc`'s idiom already: `assoc.inc` reports as a **zero row** on
`kern_small` rather than vanishing from the table, which is also the more
useful report. The one line that must sit outside the `%ifdef` is the closing
`section .text` — CLAUDE.md's section-discipline rule holds on both arms, and
the gated-out arm switches section nowhere.

W1 before W2 is not a preference: `filecp` has five entry points and fits the
mechanism as it stands, so it is the wave that proves the `%ifdef KERN_SMALL`
module shape with nothing else moving. `fdlg` then arrives against a mechanism
that has already shipped once.

### 9.3 What a `kern_small` user loses to W0

Worth stating precisely, because it is the only one of the three waves that is
visible:

- **Document icons** in the Disk window — files get the generic glyph.
  `associco.inc`'s build-time reduced glyphs go with it.
- **Double-clicking a document to open it in its program** (`ui.inc`'s
  `assoc_run_x`). A program is still launched by double-clicking the program.
- **`OSAPI_ASSOC_SET`** and **`OSAPI_ARG_FILE`** become refusing stubs. That is
  cleaner than it looks: `osapi_arg_file_x` is only ever non-empty *because* a
  document launch put something there — *"the first proc to ask gets it and the
  word is spent"* — so with no document launch, `stc / retf` is the answer it
  already gives on every other launch path, and no package needs changing.
- The mount's icon harvest loses its **cache**, not its function: `asc_lookup_x`
  exists so a package with a known association *"costs NO sector read"*. Without
  it every harvested icon is read. **Mounts get slower on the slowest machine**,
  and that is the one real cost in this list — it wants measuring on W0 rather
  than assuming.

### 9.2.3 W1 as built, and the four things §3.1 had wrong

`KERN_SIZE` **92,160 → 90,624, −1,536**; heap floor 91.5 → 90.0 KB; free heap
36.5 → **38.0 KB**. `FILECP.DRV` is 2,159 bytes, **three** entries, 5 sectors.
**`kern_big` pays 7 bytes of `.cold` and `KERN_SIZE` does not move** — the
150 bytes of `.text` the report showed beside it are `mouse.inc` +60 and
`fprog.inc` +90 from the merged pointer-tracking work, whose baseline had not
been re-blessed.

Verified by driving the real surface on kern_small under MartyPC — click,
Edit ▸ Copy, navigate, Edit ▸ Paste — for a file *and* a folder tree, with
`os88disk --verify` walking the volume afterwards. Registered as the
`fcpsmall` soak row so it cannot regress.

§3.1 said five entry points and a clean seam. Four corrections:

1. **`fcp_ncopy` is not a body.** `fcp_ncopy equ dsk_ncopy` makes it disk.inc's
   routine under a second name, resident, needing no entry and no load. The
   seam analysis counted a symbol, not a definition.
2. **`fcp_goto` cannot move.** CLONE.DRV far-calls it through `fcpf_fcp_goto`
   *between two raw transfers of a same-drive clone* (§18.99.8) — exactly when
   the system disk is not in the drive. A `mod_need` there fails `drv_mounted`
   and hands the cloner a CF=1 it can only stop on. It and its four doors stay
   resident, which takes the count to **three** and means no module ever loads
   another.
3. **Two tail jumps out of the image were missed**, and `os88ovlchk` caught
   both — `jmp dskw_stat_x` and four bare `call fcp_goto`. A near `ret` against
   a far frame returns into nothing, and neither is visible in a seam count of
   `call` sites.
4. **The image needs its own copies of the shared register epilogues.** A
   module may not `jmp kretc_cx` for the same reason.

### 9.2.4 The rule that makes a two-shape file work

Two host gates read this tree's SOURCE and can evaluate no `%ifdef`:
`tools/os88ovlchk.py` (near calls across a segment) and `tools/stkbalance.py`
(every `ret`'s depth). A file whose bodies are `.cold` on one build and `.modp`
on the other has no reading that satisfies both — writing `.cold` last made
ovlchk report the module's own jumps as crossings, and writing `.modp` last
made it report kern_big's near jumps instead.

**The resolution is a discipline, not a special case, and it is three rules:**

- **One conditional `section` per file, and the module arm goes LAST**, because
  ovlchk files everything after the last `section` directive it sees.
- **No `%ifdef` in the bodies at all.** Every build-dependent transfer goes
  through a macro (`FCPX`, `FCPBODY`, `FCPXF`), which both gates skip, so
  neither can be shown an arm that is not live.
- **A macro may never END a path.** stkbalance reads source, so a macro that
  expands to a jump or a `ret` is one it walks straight through into the next
  routine's pops — it reported five false imbalances that way. A tail call is
  therefore written `FCPX name` followed by a literal `ret`, and a shared
  epilogue is a real `jmp` to a copy inside the file.

The third rule is what costs `kern_big` its 7 bytes, and it is worth them: the
alternative is a gate that cannot see the build it is checking.

### 9.2.5 …and one that has nothing to do with the kernel

`KMODS` was gated on `KERN_SMALL`, which is right for the `os88mod.py`
arguments — that expansion happens in the make that assembles the kernel. It
is **wrong** for the floppy rules, which expand `$(SMALLDRIVERS)` in the OUTER
make where the knob is not set. FILECP.DRV was therefore left off the disk
while every build step succeeded and the machine booted, and Cut/Copy/Paste
refused with `FERR_NODISK` because `mod_need` could not find a file nobody had
shipped. `$(SMALLMODS)` names it for those rules instead.

**Nothing in the build would have caught that**, and nothing in `full` does
either — it took driving a paste on the finished floppy. It is the strongest
argument in this document for the `fcpsmall` row existing.

### 9.2.6 W2 as built — `fdlg.inc` becomes `FDLG.DRV`

SPEC.md §38.0 is the contract; this is what the wave cost and what it found.

`KERN_SIZE` **90,624 → 88,064**, and the cold rung UNCROSSES — 2,560 bytes
back on every machine that boots `kern_small`, against a `FDLG.DRV` of 3,243
bytes in 7 sectors. Free heap on a 128KB machine is **40.5 KB**. `kern_big`
pays **29 bytes** (`.bss` +12, `.cold` +17) and its `KERN_SIZE` does not move.

Four things §3.1 did not predict:

1. **Seven entries, not nine.** `fdlg_onup` and `fdlg_ondrag` are §13.10.5's
   thumb drag and already `kern_big`'s alone, so this build has no bodies for
   them. But `MOD_NENT` still had to go 6 → 7, **for both kernels**: the
   per-build value assembled fine and broke `tools/os88mod.py`, which scrapes
   the first `^MOD_NENT equ <int>` out of `mod.inc` and cannot evaluate an
   `%ifdef` — it read 7 on both arms and refused `CTRL.DRV`'s first entry as
   out of range. That is where 12 of `kern_big`'s 29 bytes go.
2. **Mixed exit conventions.** `filecp.inc`'s three entries all return near, so
   all three are wrapped. Here five of seven already `retf` and two do not, so
   the header points five bodies at **themselves** and wraps two. First module
   in the tree that needs both.
3. **The far-entry prefix collides.** `filecp.inc` uses `xf_` and both files
   wrap `dsk_ncopy`; `.cold` is one segment, so one prefix would be one label
   defined twice. This one is `xd_`.
4. **`os88ui.inc` had to be lifted out of the image first.** `apps.inc`
   near-calls `ui_krect4`, and the `%include` sat in the middle of what is now
   `.modd`. Only `tools/os88ovlchk.py` noticed.

**And two defects the static gates caught that nothing else would have.** Both
are `tools/stkbalance.py`'s, and both were introduced by applying §9.2.4's
rules mechanically:

- **Rule 3 assumes the target of a tail jump is CALLABLE, and a continuation
  is not.** `fdlg_hasdot` ended `jmp fm_dotin`, and `fm_dotin` is not a
  routine: `fm_hasdot` falls into it having banked SI, and `fm_dotin`'s own
  `pop si` takes that bank, so its near `ret` returns to *that entry's*
  caller. Rewritten as `FDX fm_dotin` + `ret` — which is what rule 3 asks for
  everywhere else — the `pop si` eats the return address, **on `kern_big` as
  much as on `kern_small`**. `fdlg_hasdot` is resident now and keeps its
  `jmp`. Reported as `fdlg_hasdot: ret at depth +1`.
- **An `equ` alias is not free even when it emits nothing.** Writing
  `fdk_bp equ kretc_bp` on the `kern_big` arm would have saved that build 13
  bytes and made `kernel.asm`'s own `kretc_*` look ADDRESSED to a source-
  reading walker, which then walks all five as routines entered at depth 0:
  ten findings for a change that altered no instruction. The ladder is an
  unconditional copy below the section toggle, exactly as `filecp.inc`'s is.

So §9.2.4's three rules stand, with one sentence added to rule 3: **check that
the target of a converted tail jump is entered by a `call` somewhere.** Both
of these were invisible to `make`, to every emulator run, and to a reading of
the diff; the gate that caught them reads source and cost 0.6 seconds.

### 9.2.7 …and a harness defect that made W2 look broken for a session

`tools/os88geom.py` mirrors the kernel's geometry so that no test writes a
constant down twice — and it mirrored **one of the two kernels** without
saying so. `WIN_SIZE` is 34 on `kern_big` and 28 on `kern_small`; the parser
took the first `equ` it saw and `verify` compared against that same first
`equ`, so the guard agreed with itself while every script pointed at
`kern_small` decoded `wm_wins` at the wrong stride. It returns NUMBERS — a
Disk window at (103, 20, 322, 155) followed by a second "window" at
(60, 552, 93, 0) — which reads exactly like the package under test failing to
launch, which is the module that had just been built.

It knows the arm now, off `$OS88_DEFINES` (os88sym.py's own knob, which every
`kern_small` row already sets), and a constant that exists on only one arm —
`vidsel.inc`'s extended-desktop record is `kern_big`'s whole — is **refused**
on the other rather than answered with the big number.

**A second trap sat behind it and is worth writing down on its own.**
`tools/os88sym.py` refuses an address unless its map matches `build/kernel.bin`
byte for byte — but the emulator boots a **floppy**, and nothing compares the
two. A `build/kernel.bin` newer than `build/os8088-360.img` therefore passes
the check and hands out addresses for a kernel that is not running, and
because `.bss` is `nobits` a pure `.bss` difference is invisible in the image
anyway. The symptom was a file dialog that painted correctly while
`fdlg_name` stayed empty and `fdlg_nlen` read 128 — the feature working and
the harness reading 24 bytes to the left. `make` fixes it; knowing to suspect
it is the expensive part.

### 9.2.8 W2's own defect, found in the field: the drop never ran

Reported as *"the `.modc` (file dialog at least) are not freeing themselves
after the dialog closes"*, and it is exactly that. §9.2.6 shipped `mod_drop`
in `fdlg_reap` and said so; what nothing checked is that `fdlg_reap` is
**reachable** at the moment the drop is due.

`[fdlg_win]` is the resident word that says a dialog is up, and §38.0's own
argument leans on it — *"the image is in RAM whenever any of the other six
has work"*. True on the way in. On the way out it inverts, and the design
did not notice: three of the four routes that end a dialog — the Open/Save
button, Cancel, and Escape — reach `fdlg_close` from **inside the image's own
`W_ONCLICK`**, so `[fdlg_win]` is already 0 by the next UI pass. `mod_drop`
sat behind three separate compares of that word (`ui.inc`'s ladder,
`fdlg_reap`'s thunk, and the top of `fdlg_reap_x`), and each turned the pass
away. Only the close/minimize box — which merely *hides* the window, so
`fdlg_gate` inside `fdlg_reap_x` is what cancels it — ever got there.

So `FDLG.DRV`'s claim was held from the first dialog to the end of the
session: **16KB of a 128KB machine, for a dialog nobody could see** — the
same failure mode §9.2.6's own mode-6 note records for `MOD_FMT`, one
feature along.

Three things in it are worth carrying to the next module:

- **The route that works is the route nobody uses.** Every hand-test of W2
  dismissed the dialog with a button. The close box is the least-used of the
  four and is the only one that was ever collected, which is why a feature
  that had been driven repeatedly still shipped this.
- **The leak is invisible by construction.** The dialog really is gone, the
  window really is destroyed, and the next dialog reuses the image it never
  gave back. No picture, no refusal, no error — only `mem_tab` knows. That is
  what makes `tests/fdlgdrop.py` assert `mod_tab[MOD_FDLG].seg` rather than
  anything on the glass, and what made `tests/small128.py`'s "no pinned claim
  on a bare desktop" miss it: the desktop is bare *before* the first dialog.
- **A resident guard has to be asked the resident question.** "Is a dialog
  up" and "is the image held" differ for exactly one pass in the life of
  every dialog, and that pass is the one that matters. SPEC.md 38.0.1 is the
  fix — `mod_r_fdlg` names the `mod_tab` row so the guard can ask the second
  question — and it costs **+0 bytes on both arms**, being the same
  instruction against a different word.

**The other three modules were audited and are sound**: `fcp_fin`,
`cpf_cp_flush_close`, `fm_fmt_drop` and `clo_drop` all hang off the
operation's own end and none is guarded on state the module clears from
inside itself.
