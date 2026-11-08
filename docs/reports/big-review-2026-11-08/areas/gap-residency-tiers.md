# Machine-shaped residency, a purgeable tier, and how far to take on-demand modules and shared code, as one memory-tier design

Gap review, 2026-11-08. No repository file other than this report was edited;
nothing was assembled, built or booted. Byte figures are SYMBOL SPANS read
off `build/kernel.map` (kern_big) and `build/smallk/kernel.map` (kern_small)
at this tree, unless marked ESTIMATED. Each span runs from a top-level label
to the next one in its section. The package figures come from parsing the 35
`build/*.lst` listings. Each one sums to its `.bin` size byte for byte, so
nothing was dropped.

## Scope read

| file | what |
|---|---|
| `kernel/mod.inc` (all 1,006 lines) | `mod_tab`, `mod_need`/`mod_check`, `mod_cfix`, `mod_hi`/`mod_hiboot`, `mod_disarm`, `mod_free_row`, `mod_drop` |
| `kernel/kernel.asm` 100-140, 1080-1175, 1660-1700, 5730-5770, 7310-7356, 8720-8870 | the ladder, `FCP_MOD`/`FDLG_MOD`, `COLD_SLIDE`, the `.vga` rung, the slide call in `kmain_o`, `MODFP`, `.cfx` and `cfx_slide` |
| `kernel/memory.inc` 700-800, 1280-1345 | `mem_floor_ax`, `mem_pg_own` (the purgeable naming rows), `MEM_K_MOD` |
| `kernel/vidext.inc` 30-140 | `VIDEXT`/`VIDEXTM`: how a 1bpp, planar or packed display is dispatched |
| `kernel/filecp.inc` 2255-2300, `kernel/fdlg.inc` 103, `kernel/xmem.inc` 1-60, `kernel/mouse.inc` (PS/2 block) | the need/drop brackets, `FD_NENT`, the `DRVC_OVL` precedent, PS/2 state |
| SPEC.md 2.8.3, 2.10.9 (3920-3980), 9.12 (17430-17520), 38.0-38.0.1 (108180-108275) | the module lifetime contract, the slide, `DRVC_POINT`/`OSAPI_MOUSE_FEED`, the FDLG split |
| docs/KERNEL-MEMORY.md 1-150, 442-500, 1064-1135 | the rules, *Where it goes*, *`.cold` moves* |
| docs/plans/completed/ONDEMAND-PLAN.md 1.2, 1.3, 7.1, 7.2; docs/plans/HEAP-UNPIN-PLAN.md 4.3, 7 (piece B), 500-520; docs/plans/MOVABLE-COLD-PLAN.md head; docs/plans/MONO-RECLAIM-PLAN.md head | the designs this gap ties together |
| docs/plans/ABI-BREAK-LIST.md, docs/plans/KERNEL-BYTE-QUEUE.md | grepped for module, purgeable, rung, PS/2, shared and instance rows |
| tests/vgarefs.txt, tests/moddrop.txt | the rung registry and the drop gate |
| areas/bytes.md (bytes-2, bytes-3, Rethinks), video.md (video-7, video-17), text.md (text-7), input.md (input-12, R2), histmem.md (hm-9, hm-11, hm-15, Rethinks), loader.md (loader-14), shell.md (shell-2), gap-os88ui-kernel-copy.md | the findings this gap prices together |

## Verdict

The pieces the earlier reviews found are real, but priced together they make
a smaller and cheaper design than "a generalised rung mechanism plus a
purgeable tier":

1. **The purgeable tier is ONE site, not a pin per thunk.** Every module
   already has a need/drop bracket around its live span, and
   `tests/unit/t_moddrop.py` guarantees the drop. If `mod_drop` re-tags the
   claim as purgeable and disarms its slots, and `mod_need`'s hit path
   re-tags it pinned and re-arms, then the existing bracket is the pin
   (rt-1). That costs about +60 to +85 `.cold` for every module at once.
   HEAP-UNPIN piece B's per-thunk count is 215-240 bytes and was priced
   when there were 29 far-call sites; there are 49 today. Caching does
   expose one new hazard: a reused image keeps its old `.bss`, and today
   "every load is a read" (`mod.inc:650-654`). So the bss has to be
   re-zeroed.
2. **bytes-3 is overstated by about a thousand bytes.** Its 2,080-byte
   `fdlg.inc` span includes 1,089 bytes of the kernel's `os88ui_*` copy,
   which `files.inc` still needs. Movable, net of the resident halves
   kern_small already keeps: about **-2,500 resident on kern_big** (rt-2).
   The boot read does not grow, because those bytes are already in
   KERNEL.SYS as `.cold`.
3. **Do not generalise the rung.** A rung can be dropped only as a suffix
   under `.cold`, at fixed near offsets, so rungs serve only NESTED machine
   classes. CPU class and adapter form a lattice, and VGA and mono are
   COMPLEMENTARY. A second rung strands one corner of the lattice, and the
   owner's 5150 #2 (8088 + PVGA1A) is the corner bytes-2's AT rung strands
   (rt-4). For class code on a cold cadence the tree already has the right
   general mechanism: an owner-loaded `DRVC_OVL` overlay, which is
   XMEM.DRV's shape. The PS/2 mouse should take it (rt-3). It saves bytes
   on every kern_big machine, VGA included, so the gauge sees it.
4. **The keyboard mouse is init-lifetime code, not on-demand code.** After a
   pointer settles it is dead for the rest of the session (`[mou_ptr]` is
   "never cleared"). So it belongs in a module that is held from boot and
   dropped at settle, which avoids input-12's disk read at the moment the
   mouse has failed (rt-7).
5. **KERN_BUDGET should stay a VGA reading, because it is a ceiling.**
   kernsize should also print the per-class figures it can already compute
   (rt-6). Otherwise the target machine's wins (the slide's 2KB, text-7)
   stay invisible.
6. **Shared code was measured, and a library does not pay on the target**
   (rt-8). Across 35 shipped packages, at least 84.0 KB of 942 KB of image
   (8.9%) is emitted from `apps/os88*.inc` source, and at least 60.6 KB of
   it is duplication. But RAM pays only for concurrently running copies,
   and a far-called library costs 4x on every call. On a one-floppy machine
   a library file is a disk swap. The levers that do pay are hm-11's
   init-drop and a purgeable decoded-image cache for relaunches.
7. **The refusal of automatic mid-gesture reload stays right** with a purge
   tier: reload happens only where `mod_need` already loads, at an entry.
   What purging adds is one bad case: FDLG or FILECP shed under pressure
   while the system disk is out. For that case the honest answer is DOS's
   COMMAND.COM answer, the existing swap prompt, not a refusal (rt-2).

### The priced combination, per machine class (resident static kernel bytes)

Today's footprint comes from the kernsize bless of 2026-11-08 (kern_big
`KERN_SIZE` 96,768 on VGA). The mono figure is ESTIMATED by ladder
arithmetic: `.vga` 2,032 and `.vgabuf` 336 come off, and `mem_floor_ax`
rounds up to 512. kern_small reads 58,880 (KERNEL-MEMORY.md:480).

| change | 8088 CGA/Herc (target) | 8088 VGA (5150 #2) | AT VGA | AT mono | kern_small 128KB |
|---|---:|---:|---:|---:|---:|
| today | ~94,400-94,720 | 96,768 | 96,768 | ~94,400-94,720 | 58,880 |
| rt-1 drop-to-cache, all modules | +60..+85 | +60..+85 | +60..+85 | +60..+85 | +60..+85 (diet: owner's call) |
| rt-2 FILECP+FDLG boot-cached modules | -2,500 | -2,500 | -2,500 | -2,500 | 0 (already modules) |
| rt-3 PS/2 runtime as an owner-loaded overlay | -220 | -220 | -220 (+~0.5-1KB claim if a PS/2 mouse is live) | -220 (+claim) | 0 |
| rt-7 keyboard mouse, held from boot, dropped at settle | -300..-380 | -300..-380 | -300..-380 | -300..-380 | -300..-380 |
| text-7 (`font_run_x`'s planar prologue into `.vga`) | -185 | +10 | +10 | -185 | n/a |
| video-7 (fsx rows on kern_small) | - | - | - | - | -82..-86 |
| **net** | **~-3,150 to -3,230** | **~-2,950 to -3,045** | **~-2,950 to -3,045** | **~-3,150 to -3,230** | **~-320 to -400** |
| seen by KERN_BUDGET (VGA) | no | yes | yes | no | yes (own budget) |

Heap at the desktop also carries about 4KB of PURGEABLE claim for the two
cached images on kern_big: 1,871 + 1,600 bytes, plus kern_big's extra
dialog arms, in 512-byte granules. `mem_avail` counts it as free, and
`mem_claim` sheds it on a refusal. kern_mega moves nothing conventional for
rt-2, because `.cold` and modules already live above 1MB there. It gains
rt-3 and rt-7 in its conventional `.text`.

## Strengths

- **The module loader is small and refuses unsafe states.** It is about
  378 bytes of `.cold` (`mod_need` 229, `mod_cfix` 59, `mod_disarm` 23,
  `mod_fpr` 19, `mod_free_row` 19, `mod_drop` 12, `mod_row` 10, `mod_live`
  7), 179 bytes of `.text` (names, `mod_tab`, `mod_fpt`, `mod_hdrt`) and 48
  bytes of `.bss` for 11 rows on kern_big. It disarms before it frees
  (`mod_free_row`), and an unloaded slot is a `stc / retf` rather than a
  jump to 0:0 (`mod_gone`). The header carries a build stamp plus a layout
  stamp, so a module from another configuration of the same commit is
  refused.
- **The VGA slide is nearly free resident.** The mover and its list are
  transient: `cfx_slide` and `cfx_tab` are in `.cfx`, 0x212 = 530 bytes in
  the file and none resident (SPEC.md 2.10.9: "It costs no resident byte
  and no blob sector"). What stays resident is `mod_cfix` (59 bytes, shared
  with ROM and HMA adoption), `mem_floor_ax`'s `COLD_SLIDE` arm (about 20
  bytes, `memory.inc:727-742`) and the `[vga_skkern]` store. The rung's
  doors were not added: they are the `VIDEXTM` dispatch the code already
  had for 1bpp, planar and packed displays (`vidext.inc:71-80`,
  `tests/vgarefs.txt`).
- **Init code is already discarded.** The boot blob (`.boot2`, 0x19FC) and
  the boot overlay (`.ovl`, 0x13FF) hold the boot work, including
  `mou_p2_init`'s 327 bytes. The largest resident symbol matching
  init/probe/boot is `fm_kinit_x`, 131 bytes, which runs per window rather
  than once. On the kernel side, hm-11's idea is already done.
- **Class code already leaves the 8088.** XMEM.DRV is an owner-loaded
  `DRVC_OVL` image, loaded only where memory above 1MB exists. Its banner
  states the principle: *"a machine with no XMS - which is every 8088 ...
  was reserving about 1.2KB of kernel image forever for a feature it can
  never reach"* (`xmem.inc:21-24`). That is the general mechanism; it just
  is not called a rung.
- **The resident rule already prices a purgeable tier honestly.** Rule 1
  counts "every non-purgeable byte", and `tests/kernresident.py` walks
  `mem_tab` (KERNEL-MEMORY.md 41-50), so a cached module costs nothing on
  the gauge exactly when it really is sheddable.
- **The drop gate is what makes caching a one-site change.**
  `t_moddrop.py` proves every `mod_tab` row reaches a `mod_drop`, and
  `tests/moddrop.txt` is empty. Every module therefore already has the
  bracket that rt-1 turns into a pin.

## Findings

### rt-1 (rethink, high, now) - drop to a cache, and let the need/drop bracket be the pin

**Evidence.**

- `mod_drop` frees: `mod_row`, `mod_free_row` -> `mod_disarm`, then
  `mem_free_x` (`mod.inc:909-972`).
- `mod_need`'s hit path is `cmp word [bx+MODR_SEG], 0 / jne .out`
  (`mod.inc:407-408`).
- The purgeable naming rows are `tag, base of a WORD array, count`
  (`memory.inc:1292-1333`). `mod_tab` is stride 4, with seg and file
  interleaved (`mod.inc:144-160`, `219-260`), so its seg words are not yet a flat word
  array.
- HEAP-UNPIN piece B prices the per-thunk pin at 215-240 bytes, on "29
  thunk sites". A grep of `call far [XXFP` today counts **49 sites through
  14 slot blocks** (CPFP 11, HBFP 8, FDFP 6, FCPFP 4, FMFP 4, DKDFP 3,
  SERFP 3, and so on). The per-site design has got dearer since it was
  priced.

**Design.**

- `mod_drop` disarms, as today, and then RE-TAGS the claim from
  `MEM_K_MOD` to a purgeable module tag instead of freeing it.
- `mod_need`'s hit path tests the tag. If it is purgeable, `mod_need`
  re-tags it pinned, re-zeroes the bss, and re-arms the slots from the
  header's entry table with `[MODR_SEG]`. That is the arm loop with the
  validation skipped. It is also correct on kern_mega, where `MODR_SEG`
  already holds the code selector after `mod_hi`.
- Split `mod_tab` into two parallel word arrays, which costs nothing
  (`mod_row`'s shift drops from 2 to 1). One `mem_pg_own` row,
  `dw MEM_P_MOD, mod_seg, MOD_MAX`, then names every image.
- `mem_pg_forget` gains a `.mod` arm that disarms before the word is
  zeroed, like its `.view` and `.fatw` arms.
- The span from need to drop is pinned because the tag is `MEM_K_MOD`.
  The per-call entries (FILECP's `fcp_load`/`fcp_fin`, `filecp.inc:2262-2291`)
  and the held spans (the panel open to close, the dialog open to reap)
  are pinned by the brackets they already have. The three thunks with no
  `mod_need` rest on `mod_gone` once dropped, exactly as today.
- No new race. A drop on a worker while the UI task is inside the image
  is today's hazard (it frees), and the caller-decides rule of SPEC.md
  2.8.3 still governs it.

**The hazard caching introduces.** `mod.inc:650-654`: *"a module's bss is
zeros in its own file ... every load is a read"*. A cached image keeps the
previous use's bss. Add a kernel-private `MOD_H_BSS` header word (module
headers are free to move: ABI-BREAK-LIST P2) and zero from it on re-adopt.
`mod_cfix` is idempotent on a re-adopt, because a word it already
re-pointed no longer reads `COLD_HOME`.

**What it buys.** It answers shell-2 for all modules, not only `MOD_SETS`
and `MOD_STIKM`: the second drag, the second Sticky Apps press and the
second Format cost no disk on a machine with room. It is ONDEMAND-PLAN
7.2's "shed, do not free", with 7.1's pin already supplied.

**Bytes (ESTIMATED).** About +60 to +85 `.cold`. The pieces are re-tag at
drop (~10), the hit-path re-tag, re-zero and re-arm (~30-40), the
`mem_pg_own` row (6) and the forget arm (~14), plus 2 bytes of header per
module image. On kern_small it is a spend the diet forbids unless the owner
trades for it.

**Listed as:** ONDEMAND-PLAN 7.1/7.2 (designed, not built); HEAP-UNPIN-PLAN
piece B (this supersedes its per-thunk pin); shell-2. It is not on
ABI-BREAK-LIST: the module header is kernel-private (P2).

### rt-2 (size, medium-high, now) - FILECP and FDLG on kern_big as modules cached at boot: about -2,500, not -3,600

**Evidence.**

- The `fdlg_ref .. desk_zs` span bytes-3 measured (2,080 `.cold`) is
  `fdlg_*` **987** plus the kernel's `os88ui_*` copy **1,089** (the button
  217, the scroll bar and its drag, `kbtn`, `bhit` and the arm; matching
  gap-os88ui-kernel-copy's table). SPEC.md 38.0: *"`os88ui.inc` stays
  resident for the reason it always did"*. Disk windows use the scroll
  bar, and kern_big's header buttons use the button. So that part does not
  move.
- FILECP is `fcp_*` 1,667 `.cold` + 152 `.bss` on kern_big. kern_small's
  resident half is 94 `.cold` + 68 `.bss`.
- FDLG is `fdlg_*` 987 `.cold` + 43 `.text` + 28 `.bss`. kern_small keeps
  91 + 41 + 15.
- The movable part is therefore about **-1,657 - 911 = -2,568**. The
  images are `build/smallk/filecp.drv` 1,871 bytes and `fdlg.drv` 1,600
  bytes (`MOD_H_IMG`); kern_big's would be a little larger (the thumb-drag
  arms).

**Shape.**

- Keep the two images in KERNEL.SYS's one contiguous read, after `.cfx`,
  instead of letting `os88mod.py` cut them out.
- At `mem_unblob`, claim them, copy them in, run `mod_check`, and then
  `mod_drop` them, so they start life cached under rt-1. The boot copy is
  transient code; boot2 or `.ovl` is the place.
- Ship FILECP.DRV and FDLG.DRV beside them for a reload after a shed. That
  is about 4 clusters on each kern_big system disk.
- **The boot read does not grow.** The bytes are in KERNEL.SYS today as
  `.cold`. bytes-3's +120 ms assumed an extra track that is not there.

**Purge policy.** Give these two the highest rank, so they are shed last,
after every cache. ONDEMAND-PLAN 1.2 and 1.3 are why: the user's disk is in
the drive when they are wanted.

**The one regression.** Suppose a package load sheds them, and later that
package's Save As runs with the data disk in. That is the worst moment,
with unsaved work. kern_small today answers it with *"opens no chooser"*
(SPEC.md 38.0). For FDLG the better answer is the swap prompt the
formatter already uses (ONDEMAND-PLAN 3.3: *"it is FS_EDIT mode 6"*):
insert the system disk, load, swap back inside the dialog. That is exactly
COMMAND.COM's transient reload: *"Insert disk with COMMAND.COM"*. Price it
at about +20-30 `.cold`.

**Bytes (ESTIMATED).** About **-2,500 resident on kern_big and kern_emu**:
-2,568 less ~60-100 of boot glue and prompt. The KERN_BUDGET gauge moves by
the whole amount. kern_mega is unchanged, since its `.cold` is already
above 1MB.

**Listed as:** bytes-3 (corrected); ONDEMAND-PLAN 1.2/1.3 (refused for
on-demand loading; this argument is boot-time caching), 7.2.

### rt-3 (rethink, medium, now) - the PS/2 runtime as an owner-loaded overlay, not an "AT rung"

**Evidence.**

- The PS/2 runtime is resident `.text`: `mou_p2_byte` 114, `mou_p2_isr`
  51, `mou_p2_off` 32, `mou_p2wr` 29, and `mou_p2cw`/`wcmd`/`dw` 25, plus
  14 bytes of state, about 265 in all.
- Its init (`mou_p2_init` 327, `knock`, `rd`, `dev`, `flush`; 435 in all)
  is already in `.ovl`.
- The tree has the general mechanism twice. XMEM.DRV is a `DRVC_OVL` image
  "loaded and attached by its OWNER", with no `drv_tab` row
  (`xmem.inc:11-19`). The pointer feed is `OSAPI_MOUSE_FEED`, with its
  apply under `cli` on `mou_pstack` (SPEC.md 9.12.1).
- bytes-2's AT rung (corrected to -250..-320) pays only on an 8088 with
  neither a VGA nor an EGA. It is 0 on the gauge and 0 on 5150 #2. It
  needs a second slide delta and an `atrefs.txt`, and its ordering strands
  a corner (rt-4).

**Shape.**

- A `PS2MOU.DRV` overlay that the mouse boot loads when `cpu_tier` is 286
  or higher and the 8042 reports an aux port. The probe that decides stays
  in `.ovl`.
- The overlay's IRQ12 handler accumulates the packet and applies it
  through the feed path.
- The resident part is `mou_p2_off`'s front (it is called unconditionally,
  `mouse.inc:1264,1437`) and the published state span of SPEC.md 9.4.4,
  which is read by offset off a floppy and must stay.
- The feed's fence (`ES == [drv_fseg6]`, DRVC_POINT's slot) needs either
  publication in that class or a kernel-armed far pointer for the overlay:
  ~10-20 bytes.

**Bytes (ESTIMATED).** About **-220 resident on every kern_big machine**,
VGA included, so the gauge sees it. That is -265 + ~30-45 of fronts and
fence. An AT with a live PS/2 mouse pays its image as a heap claim
instead. kern_small carries no PS/2 code. It also gives `OSAPI_MOUSE_FEED`
a second caller, which matters to T4's census.

**Listed as:** bytes-2 (supersedes the rung shape); ABI-BREAK-LIST T4
(`OSAPI_MOUSE_FEED`'s one caller).

### rt-4 (record, medium) - rungs serve nested classes only; video-17 is complementary to the VGA rung

**Evidence.**

- `cfx_slide` moves `.cold` alone, down by `VGA_PARA`
  (`kernel.asm:8768-8800`).
- `.vga`'s labels are fixed `KERNEL_SEG` offsets (`VGA_VSTART`,
  `kernel.asm:1679-1686`) and `.text` near-calls them.
- So a rung can be given back only when it is a contiguous suffix directly
  under `.cold`.
- Two rungs serve three of four corners:
  - With `[AT][VGA][COLD]`, the 8088 + VGA corner keeps the AT bytes.
  - With `[VGA][AT][COLD]`, the 286 + mono corner keeps the VGA bytes.
- The 8088 + VGA corner is a field machine: 5150 #2, the PVGA1A
  (docs/FIELD-MACHINES.md:163).
- video-17's mono renderer is the complement of the VGA rung, not a
  nesting. A VGA-only machine drops mono and a mono machine drops VGA.
  Whichever is placed lower can never be dropped. The 1bpp path is a
  direct `jb` to a fixed address (`VIDEXTM`, `vidext.inc:71-75`), the hot
  path of the target machine, so it cannot become a far table call (47 µs
  against a 756 µs floor).

**Conclusion.** "Machine-shaped residency" as rungs stops at one rung. A
general version needs relocation of near code: patching every rel16 into
the rung, the cfx list idea applied to displacements. That is a relocating
loader, which the package model refused for good reasons. video-17 stays
record, and its reason is the lattice, not "the slide mechanics are
substantial". The working split is:

- the rung for hot-path near code of one nested class (planar);
- `DRVC_OVL` overlays for class code on a cold cadence (XMS, PS/2);
- modules for features.

**Recommendation.** Write the nesting rule into SPEC.md 2.10.9 and
MOVABLE-COLD-PLAN, so the next "second rung" proposal is priced against
it. 0 bytes.

**Listed as:** video-17 (corrected reason), bytes.md Rethink 2.

### rt-5 (consistency, medium, now) - KERN_BUDGET is a VGA reading; make the target machine's figure visible

**Evidence.**

- KERNEL-MEMORY.md 41-46: rule 1 is stated *"on VGA (the adapter that does
  not drop a rung of its own, so the rule is stated against the
  configuration that costs the most)"*.
- `tests/kernresident.py` boots a VGA desktop.
- Every win on the target machine is therefore invisible: the slide's
  2,032 bytes, text-7's -185 (which shows as +10), and bytes-2.
- MOVABLE-COLD-PLAN's head figure (*"95,232 bytes resident against 97,280
  on a VGA or EGA machine"*) was produced once, by hand.

**Recommendation.**

- Keep the gate on VGA. It is a ceiling, and the ceiling is the maximum
  over classes.
- Have `kernsize` print a `classes` line from the constants it already
  reads: VGA, EGA (less `.vgabuf`), and mono with the slide (less `.vga`,
  `.vgabuf` and the 512 round-up), plus ROM or HMA where adopted.
- Add a soak-tier twin of `kernresident.py` on a `_herc_gla` machine.
- A mono gate is the owner's call. Reported figures cost nothing, and they
  are bytes, not rungs.

**Bytes:** 0 shipped.

### rt-6 (rethink, medium, now/record) - kern_small's module split as a run-time heap decision on both kernels

**Evidence.**

- `FCP_MOD` and `FDLG_MOD` are defined only under `KERN_SMALL`
  (`kernel.asm:1093-1111`).
- The split carries build arms everywhere:
  - `fdk_di` is a `%define` on kern_big and the image's own epilogue on
    kern_small;
  - `OS88UI_KBTNIMG` expands the button bodies twice;
  - `mod_r_fdlg` is a named row under `FCP_MOD`.

**Recommendation.**

- With rt-1 and rt-2, both kernels build FILECP and FDLG as modules from
  one arm, so the build-time choice becomes a boot-time one.
- kern_big always caches at boot (rt-2).
- kern_small could cache as well. The images would ride in its KERNEL.SYS
  read, about 7 sectors packed (1,538 + 1,696 bytes), inside the
  contiguous read. The 128KB machine would then open Save As on a data
  floppy until heap pressure, which is the very case SPEC.md 38.0 says it
  refuses today.
- That costs kern_small rt-1's +60..+85 against a diet that "spends
  nothing", so it is the owner's trade.
- `tests/small128.py`'s "no pinned claim" holds, because a cached image is
  purgeable.

**Bytes (ESTIMATED):** kern_big as rt-2; kern_small 0 static, plus rt-1's
cost if taken.

**Listed as:** bytes.md Rethink 3; ONDEMAND-PLAN 1.2.

### rt-7 (size, medium, now) - the keyboard mouse is init-lifetime code: hold it from boot, drop it at settle

**Evidence.**

- input.md R2 measures about 456 bytes (`kbm_*` spans: 599 `.text` on
  kern_big and 550 on kern_small, with shared helpers in that span) and
  proposes loading it on the first press.
- Its own verification objects: *"loading it needs a system-disk read at
  the moment the mouse has failed"*.
- But `[mou_ptr]` is "Set beside `[mou_seen]` ... never cleared"
  (`mouse.inc:6439-6447`, its declaration), and `kbm_key`'s first gate is
  `[mou_ptr]`. Once any pointer settles, the keyboard mouse is dead for
  the session.

**Shape.**

- The image rides in KERNEL.SYS (rt-2's mechanism).
- It is armed at boot as a held module.
- `mod_drop` runs at settle, inside `mou_settle`, the one routine all
  backends end in (SPEC.md 9.12.1). The image then becomes a cache under
  rt-1, or is freed.
- A machine with no mouse keeps it, as today, and never needs a disk.
- This is the DOS TSR or Windows INIT-segment discard idea, applied to the
  one resident body whose lifetime ends early.
- Caveat: on a pointer-less 128KB machine the claim stays pinned, which
  `tests/small128.py`'s no-pinned-claim assertion must allow for, or the
  row's machine must have a mouse.

**Bytes (ESTIMATED).** -300 to -380 static resident on both kernels (input
R2's figure, thunks counted), and the heap gets the image back seconds
after boot on every machine with a mouse.

**Listed as:** input-12 (inverted lifetime).

### rt-8 (record, medium) - shared code across packages, measured: real duplication, no library worth having on the target

**Measured** (host, `build/*.lst`, 35 shipped packages). Each emitted
listing line is attributed to the source file whose text matches at that
line number. Every listing's total equals its `.bin`. 132,762 bytes of
macro-substituted lines stayed unattributed, so the shared figures are
LOWER bounds.

| | bytes |
|---|---:|
| image bytes emitted, 35 packages | 941,780 |
| from `apps/os88*.inc` source (includes and their macros) | **>= 83,970 (8.9%)** |
| one copy of each file at its largest variant | 23,374 |
| duplicated | **>= 60,596** |
| per package, median / mean | 1,740 / 2,399 |

Largest contributors (file, packages, sum, largest single copy):

- `os88ui.inc`: 31 packages, 29,193 sum, 3,861 max;
- `os88partsbody.inc`: 26 packages, 12,340 sum, 2,213 max;
- `os88type.inc`: 23 packages, 6,234 sum, 3,055 max;
- `os88data.inc`: 5,075 sum;
- `os88spkfx.inc`: 4,674 sum;
- `os88spk.inc`: 4,440 sum;
- `os88line.inc`: 4,341 sum.

Shares by package: Telnet 40.2%, Audio 21.9%, Word 13.1% (7,031 bytes).

**What it would buy.** RAM pays only for packages running at the same time,
and only for their common subset. Word, Note Pad and Telnet together emit
about 15KB from shared source, but they share mainly `os88ui.inc` and a
little more, so about 3-5KB could be recovered (ESTIMATED).

**What it would cost.**

1. A library runs on its caller's DS with a CS of its own (the `OP_SEG`
   part shape, hm-15). Every library-held state word must become an
   argument or a fixed-DS-offset block in every client: the
   DGROUP/`MakeProcInstance` problem.
2. A far call is 46.7 µs against 11 µs near, on paths like `os88line`'s
   per-character loop.
3. The library is a FILE. On a one-floppy machine it is either on every
   apps disk (no disk saving) or on the system disk (a swap at launch), or
   it is resident (kernel bytes, the most expensive kind).

GEOS proved shared libraries on an 8088, and on floppies it was famous for
disk swaps. The kernel copy of `os88ui` is gap-os88ui-kernel-copy's
subject.

**The levers that pay:**

- hm-11's parts init-drop, 0 kernel bytes. The parts body is in 26 of the
  35 packages, up to 2.2KB each by this count.
- Instead of loader-14's author opt-in ("my image bytes are never
  written"), a **purgeable decoded-image cache**. The loader copies the
  pristine image into a purgeable claim before the entry proc's first
  write, keyed by the directory entry it already resolved. A second
  instance, or a relaunch after close, copies it back and makes no disk
  call; on an XT a launch is seconds and the copy about 55 ms per 20KB.
- The key is the danger: `os88disk.py` pins serials and FAT timestamps, so
  two builds of a disk can share them. The key must include a header
  checksum.

**Bytes (ESTIMATED):** library: not recommended. Image cache: +100-150
`.cold`, record until loader-14's question (how often is a second instance
or a relaunch done?) is measured.

**Listed as:** loader-14, hm-15 (both "not recommended"; this is the cheap
fraction), hm-11.

### rt-9 (weakness, low, now) - a purge rank per module is a decision the design must make, not inherit

**Evidence.**

- ONDEMAND-PLAN 7.2 puts every module at `MEM_PG_LOW`.
- HEAP-UNPIN piece B puts them at `MEM_PG_MED` and excludes FDLG
  (HEAP-UNPIN-PLAN 515: "shedding `FDLG.DRV` on kern_small breaks a live
  dialog").
- rt-1's bracket removes that exclusion's cause, because a live dialog is
  pinned by its open-to-reap span.
- The ranks still differ by what a reload costs the user:
  - Control Panel, Format, DISKOPS, HIBER, DESK and STIKMENU are wanted
    with the system disk at hand (`MEM_PG_LOW` is right).
  - FILECP and FDLG are wanted with the user's disk in the drive, so they
    should be the highest rank.
  - The keyboard mouse is never reloaded.

**Recommendation.** Two purgeable module tags, or a rank nibble in the row,
not one rank for every module. That is +4-8 bytes and part of rt-1's figure.

## Rethinks

1. **Three lifetimes, one mechanism.** Every module gets three lifetimes:
   held from need to drop; cached after drop until the heap wants it; and
   cached from boot for the modules whose disk is unlikely to be at hand
   (FILECP, FDLG) or whose life ends early (the keyboard mouse). The
   bracket the drop gate already enforces is the pin (rt-1, rt-2, rt-7).
   This is AmigaOS's library expunge at open-count zero and Windows' lock
   count on DISCARDABLE segments, at a cost of +60-85 bytes.
2. **Class residency without new rungs.** Three tools, each for the shape
   it fits:
   - one rung (planar, hot, nested);
   - owner-loaded overlays for class code on a cold cadence (XMEM.DRV, a
     PS/2 overlay), which is CONFIG.SYS's DEVICE= and Windows' MOUSE.DRV;
   - modules for features.
   The mono renderer on VGA machines is the one candidate that fits none of
   them, because it is complementary and hot (rt-4).
3. **The budget as a ceiling plus a reported floor.** Gate on the
   most-costly class and report the target's (rt-5).
4. **Package code sharing as caching, not linking.** A purgeable pristine
   image serves relaunches and second instances with no ABI change and no
   author promise (rt-8).

## Prior art

| system | what it did | what os8088 should take |
|---|---|---|
| MS-DOS | COMMAND.COM resident/transient split, reloaded from COMSPEC with a check and a disk prompt. IBMBIO per OEM. DEVICE= drivers per machine | rt-2's swap prompt instead of a refusal; rt-3's overlay is DEVICE= |
| Windows 3.0/3.1 (real mode) | NE segments PRELOAD/LOADONCALL/DISCARDABLE; a lock count pins a discardable segment; reload thunks; one DISPLAY.DRV and one MOUSE.DRV | rt-1's tag-as-pin is the lock count. Reload thunks mid-gesture stay refused. DISPLAY.DRV works only because GDI paid a far call per primitive, which the mono target cannot (rt-4) |
| OS/2 1.x | DLL code shared across processes, per-process instance data | Needs DS != CS; on the target it is outweighed by the swap and far-call costs (rt-8) |
| Mac System 6/7 | purgeable resources, `UnloadSeg`, GrowZone, a Toolbox in ROM | `ROM_COLD` is already the ROM Toolbox; rt-1 is purgeable CODE with an explicit `UnloadSeg` (`mod_drop`) |
| AmigaOS | libraries opened by count, expunged under memory pressure at count zero | rt-1 exactly: need/drop is open/close, and the shed is the expunge |
| GEOS (8088) | geode libraries, discardable and swappable resources | proves libraries on an XT; on floppies they were swaps, which is rt-8's caution |

## Doc/code mismatches

1. **kernel/kernel.asm:1108.** It says *"SEVEN entries, because SPEC.md
   13.10.5's thumb drag is kern_big's already"*. The code has
   `FD_NENT equ 5` (`kernel/fdlg.inc:103`), and the built
   `build/smallk/fdlg.drv` header's entry count is 5. **Proposed:** *"FIVE
   entries (`FD_NENT`), because SPEC.md 13.10.5's thumb drag is kern_big's
   already and takes fdlg_onup and fdlg_ondrag with it."*
2. **SPEC.md:108269-108270.** It says *"The claim is the image rounded up
   to whole KB, and `FDLG.DRV`'s image is 3,243 bytes."* But `mod_need`
   rounds through `mem_bytes_gp_x`, "whole granules" (`mod.inc:454`), with
   `MEM_GRAN equ 32` paragraphs (`memory.inc:468-469`), and
   `build/smallk/fdlg.drv`'s `MOD_H_IMG` is 1,600. **Proposed:** *"The
   claim is the image rounded up to whole 512-byte granules (§50.2.1), and
   `FDLG.DRV`'s image is 1,600 bytes at this writing."*
3. **SPEC.md:3937-3938 (2.10.9).** It says *"confirms the high byte and an
   opcode three bytes back that reads FA once OR'd with 70h (9A, EA and six
   opcodes no module puts there)"*. `mod_cfix` now tests exactly 9A or EA
   (`xor al, 0x9A / je .hit / cmp al, 0x9A ^ 0xEA`, `mod.inc:696-702`), and
   its own comment refutes "no module puts there": *"two of the other six
   are 8A ... and BA ...: every module is full of both"*. The section's
   *"+52 bytes of kern_big's `.cold`"* is also 59 by today's map.
   **Proposed:** *"confirms the high byte and an opcode three bytes back
   that is exactly 9A (call far) or EA (jmp far) - the OR-with-70h test it
   replaced also read 8A and BA, which every module is full of - writes
   `[api_coldseg]` and steps over the word ... 59 bytes of kern_big's
   `.cold` at this writing."*
4. **kernel/memory.inc:705 (`mem_floor_ax`).** It says *"out: AX =
   HEAP_SEG, or one VGABUF rung below it on a machine with no VGA"*. Under
   `COLD_SLIDE` with neither a VGA nor an EGA, the routine returns
   `VGA_RAM`, or `VGA_RAM + COLD_PARA` when `.cold` is still in RAM
   (727-742). Under `ROM_COLD` the base is `[rom_vgabw]`, and under
   `LADDER_PACK` everything is rounded up to 512. **Proposed:** *"out: AX =
   the heap's first paragraph, rounded up to 512 bytes: past `.vgabuf` on a
   VGA; under it with no VGA; on the VGA rung itself, past `.cold` slid onto
   it or with `.cold` gone to a ROM or the HMA, with neither a VGA nor an
   EGA."*
5. **kernel/mod.inc:169-170.** It says *"in .text it would be ~430 bytes
   against the 438 this kernel has left under KERN_CODE_MAX"*. Today's
   window has thousands of bytes free (bytes-2 quotes 6,946 from
   kernsize's window line). This is a dated rationale stated as present
   fact. **Proposed:** *"in .text it would have been ~430 bytes against the
   438 the kernel had left under KERN_CODE_MAX when it was written, and that
   guard cannot be raised by anybody."*
6. **docs/plans/HEAP-UNPIN-PLAN.md:827 and 1338.** These say *"`resb
   MOD_MAX` in `.bss` (4 on kern_big, 5 on kern_small)"* and *"29 thunk
   sites"*. `MOD_MAX` is `MOD_NIMG + 1` = 11 on kern_big and 7 on
   kern_small (`mod.inc:42-72`), and `call far [XXFP` occurs at 49 sites
   through 14 slot blocks. **Proposed:** *"`resb MOD_MAX` in `.bss` (11 on
   kern_big, 7 on kern_small at 2026-11-08)"*, and on piece B's row: *"29
   thunk sites when priced; 49 across 14 slot blocks at 2026-11-08, so the
   per-site pin has grown with every module since"*.
7. **docs/KERNEL-MEMORY.md:135-141 ("Which guard binds").** It says *"As
   blessed ... `kern_big` | 97,792 ... `kern_small` | 62,976"*. The bless
   this file carries (its kernsize JSON, `big.ksize`) is 96,768, and the
   same file's own ladder line has kern_small at 58,880 (:480). The table
   is the 2026-10-11 reset's. **Proposed:** retitle it *"At the 2026-10-11
   budget reset"* and add *"run `tools/kernsize.py` for today's - kern_big's
   `KERN_SIZE` is 96,768 at the 2026-11-08 bless"*, or refresh the rows at
   the next bless.

## Open questions

1. rt-2: does the owner accept a Save As on a kern_big machine under heap
   pressure becoming a system-disk swap prompt, in exchange for ~2.5KB on
   every kern_big machine?
2. rt-6: is kern_small's first-use Save As on a data floppy worth rt-1's
   +60..+85 against its diet?
3. rt-3: should the PS/2 overlay be published in `DRVC_POINT`, which would
   make USBMOUSE.DRV and it mutually exclusive, with no 8088 having both,
   or should it take a kernel-armed feed pointer like XMEM.DRV's private
   attach?
4. rt-8: how often are second instances and relaunches done? This is
   loader-14's measurement, and it decides the image cache.
5. rt-5: should the mono figure be gated, or only reported?

## Verification

Adversarial pass, 2026-11-08, at `4b9f1715`. Every high and medium finding
was re-read in the code on kern_big, kern_small and, where it applies,
kern_mega, and spans were re-measured from `build/kernel.map` and
`build/smallk/kernel.map`. rt-9 (low) was spot-checked. Nothing is refuted.
Neither ABI-BREAK-LIST.md nor KERNEL-BYTE-QUEUE.md lists any of these designs.
Every byte figure is still ESTIMATED: none of them is built. The corrected
fields are in `data/gap-residency-tiers.json`.

Findings:
- **rt-1** corrected. The hit path, the absence of bss zeroing, disarm-before-free, the 49 far-call sites over 14 FP blocks and the stride mismatch between mem_pg_own and mod_tab all hold. Three corrections:
  - It overturns a recorded decision. The pin is "deliberately not built" in mod_drop's banner and in SPEC.md 2.8.3. The banner's reason still holds, because the heap would purge only a dropped image.
  - kern_mega's above-1MB module images are freed through `mega_hifreef`, so mem_pg cannot purge them.
  - "mod_cfix is idempotent on a re-adopt" is unproven, because `cfix_prove` models one pass over an unfixed image. A re-adopt should skip mod_cfix.
- **rt-2** corrected. The arithmetic stands: fdlg_* 987 + os88ui_* 1,089 = bytes-3's 2,080. FILECP 1,657 + FDLG 911 = -2,568. The reviewer's .text figures were 5 high on each kernel, and the errors cancel. Two corrections:
  - Where a ROM or COLDHMA.SYS adopted `.cold`, these bytes are already off conventional RAM, so the saving is 0 and the boot-time cache claim would be pure cost.
  - SPEC.md 38.0 records kern_big's "NOT a module, on purpose". That makes this an owner decision, not a size pass.
- **rt-3** confirmed. The resident PS/2 runtime is 251 bytes of code plus about 14 of state, and its init is in `.ovl`. On kern_mega, IRQ12 may already be owned by a package, so an overlay ISR fits there too.
- **rt-4** confirmed. cfx_slide applies one delta to `.cold` only. VIDEXTM's 1bpp leg is a near `jb`.
- **rt-5** confirmed. The rule-1 sentence is now at KERNEL-MEMORY.md:52. kernresident takes `--machine`, but suite.py registers only the VGA row, and kernsize prints no per-class line.
- **rt-6** confirmed. Image sizes are as quoted. It inherits rt-2's ROM/HMA caveat.
- **rt-7** corrected. `[mou_ptr]` is never cleared and gates kbm_key. But mou_settle is kern_big's alone: kern_small inlines MOU_SETTLE_STORES at mouse.inc:3933, and kern_emu's vmm_boot_x stores `[mou_ptr]` itself. The drop therefore belongs at every store site, not in one routine.
- **rt-8** corrected. 35 listings exist against 60 `.o88` in build/, so 941,780 is a sample's total, not "of 942 KB" of everything shipped. The lower bound and the recommendation stand.
- **rt-9** confirmed (spot-check).

Doc mismatches (7), carried as 9 fixes. Each old_text was checked as unique
in the current file:
- kernel.asm's "SEVEN entries" → FIVE. `fdlg_ondrag` no longer exists, so the clause was rewritten rather than renumbered.
- SPEC.md's FDLG claim parenthetical. This one is corrected: the incident's "4KB claim" was right at the time, so the fix keeps the history and adds today's 512-byte granule and 1,600-byte image.
- SPEC.md 2.10.9's mod_cfix predicate, plus a separate fix for its "+52 bytes" (59 today).
- memory.inc's mem_floor_ax `out:` line. This is a comment-only change.
- mod.inc's "438 this kernel has left" → past tense. This is a comment-only change.
- HEAP-UNPIN-PLAN's `MOD_MAX` counts. This one is corrected: kern_small's `MOD_MAX` is 6, not 7, because OS88_SHORTCUTS is kern_big's alone.
- HEAP-UNPIN-PLAN's 29-site count, annotated with today's 49.
- KERNEL-MEMORY.md's "As blessed" table. This one is corrected: the reviewer's "At the 2026-10-11 budget reset" relabel is wrong, because the table's budgets (100,864 and 64,000) are not that reset's. The table is labelled as an earlier bless instead, pointing at the kernsize block.

Not proposed: KERNEL-BYTE-QUEUE.md:158-168's `MOD_NENT`/`FD_NENT` paragraph
("FDLG.DRV publishes 7 on kern_small") is stale in the same way. SPEC.md 2.8.1
removed the entry cap, and FD_NENT is 5. It falls outside this gap's brief, so it
is left for the queue's owner.
