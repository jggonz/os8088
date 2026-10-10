# System-side size pass 1 — measured 2026-10-04

The first dedicated size pass on what belongs to the system but is NOT resident
kernel: the on-demand kernel modules (SPEC.md 2.8), the drivers on the system
disk (SPEC.md 51), and the two SYSAPPS packages in `SYSTEM/`. Nine earlier
passes were on resident kernel memory; these items had only had side glances.

- **Tree**: branch `kernel-side-size-p1`, cut from `elendilon` at `b43231e`.
  "Before" is `b43231e` built in its own worktree; "after" is the branch with
  all 21 items merged (`2e19eda` and the commits after it, none of which move
  an item's bytes).
- **Box**: the 4-core cloud container this session ran in. Every number below
  is counted, not timed, so the box does not move it.
- **Method**: one agent per item, each in its own git worktree, target 30% with
  "no failures, no feature removed, hot paths not slower, resident kernel not
  grown". Each item was merged after its own soak rows passed; the merged tree
  was then built on all three kernels and run against the union of every
  item's rows.

## The table

`RAM` is what the item occupies when loaded: a module's unpacked image (a
module has no bss field - byte +31 of its header is code), a driver's image
plus the bss `os88drv.py` strips and `drv_bss` restores, a package's image +
bss. `claim` is what the heap actually hands over, in whole KB
(`mem_bytes_kb_x` rounds up), and is the number a machine feels. `file` is the
bytes on the floppy (LZ4 `'CZ'`, except kern_big's `CTRL.DRV`, which ships
plain on purpose - SPEC.md 2.8.7).

| item | RAM before | RAM after | % | claim KB | file |
|---|---:|---:|---:|---|---|
| CTRL.DRV (kern_big) | 12,220 | 11,649 | -4.7% | 12 → 12 | 12,220 → 11,649 |
| CTRL.DRV (kern_small) | 5,735 | 5,347 | -6.8% | 6 → 6 | 5,239 → 5,078 |
| FORMAT.DRV | 1,298 | 1,150 | -11.4% | 2 → 2 | 1,220 → 1,109 |
| CLONE.DRV (kern_big) | 10,073 | 9,551 | -5.2% | 10 → 10 | 8,529 → 8,335 |
| CLONE.DRV (kern_small) | 9,204 | 8,684 | -5.6% | 9 → 9 | 7,791 → 7,604 |
| HIBER.DRV | 6,662 | 5,499 | -17.5% | **7 → 6** | 5,103 → 4,823 |
| DOCK.DRV | 2,515 | 2,256 | -10.3% | 3 → 3 | 2,235 → 2,046 |
| EXTD.DRV | 1,566 | 1,069 | -31.7% | 2 → 2 | 1,378 → 1,064 |
| FILECP.DRV (kern_small) | 2,237 | 1,900 | -15.1% | **3 → 2** | 2,052 → 1,752 |
| FDLG.DRV (kern_small) | 1,286 | 1,164 | -9.5% | 2 → 2 | 1,239 → 1,125 |
| SOUND.DRV | 7,721 | 6,483 | -16.0% | **8 → 7** | 5,997 → 5,474 |
| HDD.DRV | 5,120 | 3,584 | -30.0% | **5 → 4** | 3,600 → 2,994 |
| HDDTOOL.DRV | 19,867 | 17,398 | -12.4% | **20 → 17** | 14,317 → 13,995 |
| NET.DRV | 6,650 | 4,590 | -31.0% | **7 → 5** | 5,148 → 3,754 |
| RAMDISK.DRV | 8,952 | 7,533 | -15.9% | **9 → 8** | 5,210 → 4,577 |
| RAMPAGE.DRV | 3,697 | 2,600 | -29.7% | **4 → 3** | 3,024 → 2,262 |
| ETHER.DRV | 19,034 | 17,390 | -8.6% | **19 → 17** | 11,894 → 11,159 |
| XMEM.DRV | 1,568 | 1,014 | -35.3% | **2 → 1** | 1,317 → 966 |
| SAVER.DRV | 15,001 | 10,794 | -28.0% | **15 → 11** | 8,447 → 7,565 |
| USBMOUSE.DRV | 1,648 | 1,152 | -30.1% | 2 → 2 | 1,390 → 1,120 |
| VMMOUSE.DRV (kern_emu) | 521 | 357 | -31.5% | 1 → 1 | 497 → 357 |
| TASKMGR.O88 | 11,514 | 9,930 | -13.8% | ~12 → ~10 | 7,530 → 7,133 |
| THEWIRE.O88 | 15,975 | 14,795 | -7.4% | ~16 → ~15 | 10,325 → 10,206 |
| **total** | **170,064** | **145,889** | **-14.2%** | **176 → 155** | **125,702 → 116,147** |

Not in the table, because it is not an image: The Wire's catalog claim, held
while its window is open, was a fixed 16KB and is now the catalog's own size
(2KB on the test fixture) - about 14KB more than the region row shows.
ETHER.DRV's socket pool (14KB at its top rung) is unchanged.

Seven items met 30% on RAM (EXTD, HDD, NET, XMEM, USBMOUSE, VMMOUSE, and
RAMPAGE at 29.7%, SAVER at 28.0% close behind). Twelve items crossed at least
one whole-KB claim boundary; the claim sum is down 21KB.

### The resident kernel

No resident section grew on any build. Two items are resident `.cold` on
kern_big (FILECP and FDLG are modules only on kern_small), so their savings are
resident there:

| build | text | bss | cold | lowbss | ovl | ovlw | sum | KERN_SIZE |
|---|---|---|---|---|---|---|---|---|
| kern_big | +0 | -15 | -451 | +0 | +0 | +0 | **-466** | 99,840 → **99,328** |
| kern_small | +0 | -15 | -35 | +0 | +0 | +0 | -50 | 65,536 (no rung) |
| kern_emu | +0 | -15 | -451 | +0 | +0 | +0 | -466 | -512 |

The baseline in docs/KERNEL-MEMORY.md was NOT re-blessed by this pass, so
`kernsize` keeps printing these deltas until somebody does.

## Why most items stopped short of 30%

The same four reasons recur, and they are the useful finding:

1. **The claim is whole KB.** Byte wins move RAM only when they cross a KB.
   FORMAT, DOCK, EXTD, FDLG, CTRL and CLONE all lost bytes and kept their claim
   (EXTD is 45 bytes from 1KB, CTRL 385/227, CLONE 335/492, DOCK 208, FORMAT
   126). HDDTOOL's 17KB claim is only 10 bytes under the next KB.
2. **What is left is content or hot code.** Boot-sector templates, user-visible
   strings, the sector/packet/sample/paint loops that the brief kept off-limits.
3. **About 900-2,300 bytes per package-shaped item is `apps/os88ui.inc`**, a
   shared include the brief kept off-limits (list (b) below).
4. **The kernel modules had already been through earlier passes**; the drivers
   had not, and the drivers are where the large cuts came from.

## Defects found and fixed on the way

- **EXTD.DRV `wm_fit_box`**: popped the banked display index into AX after
  loading AX with display 1's left edge, so a window on the second card was
  fitted against `(W_X & 0xFF00) + index`. Own commit `3f3ea6c`.
- **CLONE.DRV Uncompress**: left the 16-byte document-glyph block out of a
  package's clear prefix, so `DOS.O88` and `VIDEO.O88` (flags 0x2B) were
  refused with "Cannot expand this one" (on `b43231e`, verified). They now
  uncompress, run, recompress and uncompress again byte-identically.
- **RAMPAGE.DRV**: a dialog button's CF=1 made the caller "redraw" a rect read
  out of the text `RAMDISK.RAM` through a predicate pointer past its table; on
  VGA the page drew over the Preserve As chooser and the chooser could not be
  committed (seen on `b43231e` while checking the Xms store).
- **HIBER.DRV**: an unreachable driver volume wrote its row number as `DV_KIND`
  into the DOS handoff table (row 2 would read as DVK_FILE); it writes DVK_FREE.
- **HDD.DRV**: the BIOS path's CHS-out-of-range failure returned whatever was
  in AL; it returns status 04h.
- **NET.DRV**: the socket gate handed a package's BX back as verb*2 where
  `netpkg.inc` promises the caller's BX.
- **FILECP**: `fcp_file2` clears `[fcp_pend]` on every path, so a failed
  replace cannot leave a stale overwrite question; the cluster span is carried
  in DX across `fcp_goto`, whose FSV arm may clobber CX/SI/DI.
- **The Drivers page's `DRVM_IMG_*`**: RAMDISK and NET shrank across a KB and
  their agents did not update the constants (9 → 8, 7 → 5) because they
  believed `make`'s fast tier runs `t_drvmem`. **It does not - `drvmem` is a
  soak row.** Fixed at the merge.

## Found, NOT caused by this pass, NOT fixed

- **`fcpcopy` (kern_big) loses 4 clusters** after a folder paste - identical on
  unmodified `b43231e`. Suspect: the WRITE_SEQ stream close
  (`dwf_dskw_write_seq` / `[fcp_made]`) during a folder walk; kern_small, which
  uses `dskw_append`, does not show it.
  **CORRECTION (same day, `619ffaa`): not a product defect.** The row flushed
  the disk while the paste was still running (`settle` returns early because a
  paste holds the gfx lock), so it photographed a HELD `WRITE_SEQ` chain that
  SPEC.md 18.4.9 links only at the close - crash-consistent by design. Waited
  out on `[fcp_busy]`, the volume is clean and every copied byte matches.
  `fcpsmall`'s intermittent failure at the merge was the two arms sharing one
  scratch image across parallel lanes (`b2b9551`), and the 3/6 rates sampled
  for it were `os88bisect sample` counting its own build-race SKIPs as
  failures (`342d78b`).
- **HIBER.DRV `hb_dosseg`** is a far pointer to the DOS box's region, and
  `hbm_dosrun`'s `mem_claim` for the extent list can compact; if the region
  moves, `hbm_wake` pokes the exit code at the stale segment. The fix wants a
  relocation hook, which is resident bytes.
- **RAMPAGE.DRV**: after Esc on the Open dialog the Load button stays pressed
  (the dialog restores pixels saved while it was down).
- **`tests/saver.py --machine os8088_xt_vga`** fails its boot gate ("desktop: 0
  lit") on `b43231e` as on the branch: the row's lit-pixel reader does not read
  a VGA desktop. The registered `saver`/`dispsaver` rows and the Hercules run
  pass.
- **`rdmove`** went red on the branch with "arena moved NO - the run proves
  nothing". Compaction was correct: the row's hole under the RAM disk's arena
  existed on `b43231e` only as a coincidental 4KB between heapfrag's pinned 57KB
  block and the arena, and the larger heap made heapfrag's blocks 58KB and
  closed it. FIXED in the test (`d703160`): heapfrag A holds the floor under
  the store, heapfrag B opens above it, A closes and B's key requests the
  compaction - the store now moves 201KB down on both `b43231e` and the branch,
  goes red on a no-op `rd_reloc` and on an undeclared store, and the
  `HEAPCOMPACT=0` A/B still holds still.

## Lists

### (a) Wins that would COST resident kernel bytes - not taken

| win | resident cost | buys |
|---|---|---|
| `mod_need` claims the WHOLE module while a desktop-shortcut gesture reads only CTRL.DRV's settings core | ~6-10 `.cold` | 131 bytes of zeros (`cpc_buf`) off every CTRL.DRV load |
| ...or a compressed kern_big CTRL.DRV with prefix framing | a format change | CTRL.DRV's disk bytes |
| `apic_`-style labels for `thm_ink`/`thm_bg`/`wm_top`/`wm_fs_vis` so modules use existing API slots | 0 for routines that already have a slot; 6+ per new slot | ~10-byte stub per call target in each module |
| publish `fm_seed`/`fm_layout`/`fm_mount`/`fm_open_sel` as far entries shared by FILECP and FDLG | ~4 each | image bytes in both |
| a resident `cw_vid_disp_of` shim | ~4 `.text` | EXTD's 12-byte stub and a faster hop |
| far `dsk_rd1`/`dsk_wr1` in `.cold` | ~8-10 | 24 bytes of FORMAT.DRV |
| a shared not-bootable boot-sector template | ~116 | 116 of FORMAT.DRV - a wash |
| `MOD_BSS` zeroing on kern_big | ~20 `.cold` | modules stop zeroing their own bss |
| gate `snd_tick`'s `DSV_TICK` far call when no stream is open | a few | the per-tick call and its 16 bytes of every task slice (FIELD-NOTES 42) |
| a relocation hook for HIBER's `hb_dosseg` | small | fixes the latent defect above |

### (b) Shared-include wins

- **`apps/os88ui.inc`** is the largest remaining term in nearly every
  package-shaped item: the scroll bar (~560 in TASKMGR), the About card (~260),
  `os88ui_bdraw` (277), the glyph (~321).
  - `os88ui_bfind` (33 bytes) is still assembled under `OS88UI_NOGEST`, where
    nothing calls it.
  - `OS88UI_NOGEST` should not require `OS88UI_ARM` for a package with no
    button table.
  - Radio-only / check-only gates for `os88ui_glyph`: ~40-100 bytes a copy (ETHER,
    RAMPAGE, est.).
  - A shared save/restore ladder (`wr_sv`, `hd_ent`, `tm_pop_*`, `ep_*` and
    `kentm_*` were each written locally in this pass): ~30-400 bytes per large
    includer.
- **`drivers/os88drv.inc`**:
  - `OS88_DRIVER` always declares a 36-byte service table (`DSV_SIZE`); a
    real-length parameter saves ~10-30 RAM bytes per driver.
  - Driver-side `CLC_OR_STC` and the `kentc_bp`/`kret_*` ladder, which HDDTOOL
    had to copy locally - several hundred bytes across drivers.
  - An `OS88_STATE` convention (zero-only state last, one fill at attach) - SOUND
    and NET did it by hand and it is the cheapest win a driver has.
- **`drivers/wirezone.inc`**: 380 of its 564 bytes are six icon tiles whose
  masks and rows are largely constant; ~100-200 bytes in each of NET.DRV and
  ETHER.DRV.
- **`tools/os88drv.py`** strips only TRAILING zeros, so a driver's internal
  zero tables ship in its file.
- **`apps/os88sock.inc`**: `net_find` could also return the `NETV_STATE` socket
  check (~15 bytes a package). **`apps/os88api.inc`**: an ES-swapping
  `OSAPI_FILE_FIND` helper (six instructions per caller).
- **`drivers/net/lplink.inc`**: two near-identical candidate walks, ~60 bytes,
  but the DOS end assembles the same file.

### (c) Resident wins seen, not taken

- The resident stubs for FORMAT (`dskw_format`/`_fmt_probe`/`_reach`/`_line`)
  and HIBER (`hbf_paint`/`onclick`/`onkey`) could each be one table-indexed
  thunk: ~10 bytes of `.cold` each.
- kern_small's four `fcp_*` loader stubs could share one dispatch: ~10 bytes.
- `drv_find` (`drvvol.inc`) and `fdlg_reap` both decode the CZ size hint out of
  `dskw_raw`; one helper is ~10 bytes on kern_big.
- `hb_wakep` (4 bytes of `.bss`) may be dead now - unverified.
- `kernel/vmmouse.inc`'s `vmm_poll` comment still describes a scratch buffer the
  image no longer has.

## Coverage

Each item ran its own soak rows (listed in its merge commit's branch). The
merged tree then ran: `make` (fast tier 55/55) on kern_big, `make small`,
`make emu`; the 27 host-side unit rows in the soak tier (all ok, including the
`drvmem` that caught the constants above); and the union of every item's
emulator rows through `tools/os88soak.py` - **159 rows, 158 ok**, the one
failure being `rdmove`, since fixed in the test and green with its 11 heap
neighbours (above). Two claims no row covered were driven by hand
on the merged tree and passed: the RAM disk's Xms store end to end on QEMU
(136KB and 16MB), and Uncompress/Compress round trips of `DOS.O88` and
`VIDEO.O88` on MartyPC, red on `b43231e`.
