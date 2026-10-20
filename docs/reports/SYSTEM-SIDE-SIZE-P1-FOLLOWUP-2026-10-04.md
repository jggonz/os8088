# System-side size pass 1, follow-up round — measured 2026-10-04

The second round of `docs/reports/SYSTEM-SIDE-SIZE-P1-2026-10-04.md`: its
sections 2 and 3, which were the cross-system and shared-include wins that
round's agents listed rather than took, and the resident crumbs it found on the
way. That report is a measurement of the tree it was taken on and is not
edited; this one is the same measurement of the tree after.

- **Tree**: branch `kernel-side-size-p1`. "Before" is `f3f894c` (the first
  round merged, plus `elendilon`'s MartyPC keyboard fix) built in its own
  worktree; "after" is `48d0380`, every item below merged.
- **Box**: the same 4-core cloud container. Every number is counted, not
  timed, except the paint costs, which are guest cycles on MartyPC.
- **Method**: one agent per item in its own worktree, cut from `f3f894c`.
  Resident kernel bytes could not grow on any build, except for two costs the
  owner approved with the items (a 4-byte `cw_vid_disp_of` shim, up to 10
  bytes for `mod_need`). Each item merged after its own rows passed; the merged
  tree ran the union.

## The table

RAM is image + bss (what a load claims before KB rounding); file is the bytes
on the floppy. Drivers and SYSTEM/ packages are kern_big's, modules marked
small are kern_small's.

| item | RAM before | RAM after | % | claim KB before→after | file before→after |
|---|---:|---:|---:|---|---|
| CTRL.DRV big | 11,649 | 11,119 | -4.5% | 12 → **11** | 11,649 → 11,119 |
| CTRL.DRV small | 5,347 | 4,363 | -18.4% | 6 → **5** | 5,078 → 4,186 |
| FORMAT.DRV | 1,150 | 1,136 | -1.2% | 2 → 2 | 1,109 → 1,097 |
| CLONE.DRV big | 9,551 | 9,214 | -3.5% | 10 → **9** | 8,335 → 8,217 |
| CLONE.DRV small | 8,684 | 7,199 | -17.1% | 9 → **8** | 7,604 → 6,539 |
| HIBER.DRV | 5,499 | 5,489 | -0.2% | 6 → 6 | 4,823 → 4,814 |
| DOCK.DRV | 2,256 | 2,246 | -0.4% | 3 → 3 | 2,046 → 2,046 |
| EXTD.DRV | 1,069 | 1,023 | -4.3% | 2 → **1** | 1,064 → 1,023 |
| FILECP.DRV small | 1,900 | 1,940 | +2.1% | 2 → 2 | 1,752 → 1,892 |
| FDLG.DRV small | 1,164 | 1,208 | +3.8% | 2 → 2 | 1,125 → 1,178 |
| SOUND.DRV | 6,483 | 6,469 | -0.2% | 7 → 7 | 5,474 → 5,462 |
| HDD.DRV | 3,584 | 3,584 | 0 | 4 → 4 | 2,994 → 2,994 |
| HDDTOOL.DRV | 16,995 | 16,995 | 0 | 17 → 17 | 13,743 → 13,743 |
| NET.DRV | 4,445 | 4,153 | -6.6% | 5 → 5 | 3,652 → 3,613 |
| RAMDISK.DRV | 7,533 | 5,247 | **-30.3%** | 8 → **6** | 4,577 → 4,559 |
| RAMPAGE.DRV | 2,375 | 2,375 | 0 | 3 → 3 | 2,141 → 2,141 |
| ETHER.DRV | 17,125 | 16,778 | -2.0% | 17 → 17 | 11,001 → 10,975 |
| XMEM.DRV | 1,014 | 1,014 | 0 | 1 → 1 | 966 → 966 |
| SAVER.DRV | 10,439 | 10,403 | -0.3% | 11 → 11 | 7,346 → 7,388 |
| USBMOUSE.DRV | 1,152 | 1,151 | -0.1% | 2 → 2 | 1,120 → 1,119 |
| VMMOUSE.DRV | 357 | 357 | 0 | 1 → 1 | 357 → 357 |
| TASKMGR.O88 | 9,746 | 9,746 | 0 | 10 → 10 | 7,038 → 7,038 |
| THEWIRE.O88 | 14,309 | 14,289 | -0.1% | 14 → 14 | 9,946 → 9,941 |
| BROWSER.O88 | 19,762 | 19,757 | 0 | 20 → 20 | 13,144 → 13,139 |
| TELNET.O88 | 31,861 | 31,856 | 0 | 32 → 32 | 12,879 → 12,872 |
| MIDIRACK.O88 | 46,302 | 46,286 | 0 | 46 → 46 | 29,243 → 29,254 |
| **total** | 241,751 | 235,397 | -2.6% | 252 → **245** | 170,206 → 167,672 |

**Seven whole KB of claim**, which is the figure that is RAM: CTRL and CLONE on
both kernels, EXTD, and RAMDISK's two. RAMDISK's is the idle figure - the
2,304-byte directory now rides the chain-table claim taken at Mount
(SPEC.md 62.9.13.1), so a mounted disk costs what it did and the largest store
costs 1KB more (12 → 13KB), the directory sharing one rounding with the chain
table instead of none.

FILECP and FDLG grow on purpose: each took a 40-byte image-side dispatcher
(`fcpx_go`, `fdx_go`) so that 44 resident thunks could leave kern_small's
`.cold` (below). Neither crosses its claim.

### The resident kernel

| build | text | bss | cold | KERN_SIZE |
|---|---:|---:|---:|---|
| kern_big | 44,285 → 44,289 (+4) | 5,135 → 5,131 (-4) | 37,731 → 37,713 (-18) | 98,816 = |
| kern_small | 32,905 → 32,904 (-1) | 3,102 = | 24,130 → 23,937 (-193) | **65,536 → 65,024** |
| kern_emu | 44,550 → 44,554 (+4) | 5,135 → 5,131 (-4) | 37,855 → 37,837 (-18) | 99,328 = |

kern_big -18 net, which includes both approved costs (+4 shim, +1 `mod_need`);
kern_small -194 and one `.cold` rung back, **512 bytes of heap on the 128KB
machine**. `.lowbss`, `.ovl` and `.ovlw` did not move on any build. The
`kernsize` baseline in docs/KERNEL-MEMORY.md was re-blessed at `48d0380` for
all three variants, so builds now report against this tree and not the one
before round 1.

## What each item found

**2.1 RAMDISK's directory at Mount** - the estimate held (-2KB idle). Rows are
`index * RDE_SZ` through ES for the length of a verb, which is safe because no
verb claims (SPEC.md 66.3); the claim was already movable through `rd_reloc`.
The RAMPAGE "image stays loaded after Cancel" defect from round 1 went with it:
`[rd_pdlg]` is deleted, which is both the fix and a saving.

**2.2 `drivers/os88drv.inc`** - all three helpers added, and **all three
estimates were wrong**, -106 bytes of RAM across every driver against the
several hundred expected. The service table's last cell (`DSV_PKGCALL`) is in
use by every driver but HDD, so a real-length table shortens one, and HDD's
saving lands in padding before a 512-aligned buffer. Zero-only state placed
last saves FILE bytes and no RAM, because the claim is KB of image + bss
wherever a cell sits. The shared exit ladder is real but ETHER already had its
own. The kernel already made a short table safe (`drv_publish` copies only +15
bytes), so none of it cost a resident byte.

**2.3 `drivers/wirezone.inc`** - -292 bytes in each of NET.DRV and ETHER.DRV,
over the 100-200 estimated: the reel has eight distinct rows, so each half is
an eight-word table and each band a run list. **The one speed trade of the
round**: a zone paint is +1.7 ms (CGA) to +2.1 ms (VGA, Hercules), ~7-8% of
the zone's draw, on desktop repaints only. Pixel-identical on all three
adapters on MartyPC and on QEMU's VGA for ETHER.DRV, with a red control.
`net.bin`/`ether.bin` had not listed the include as a prerequisite.

**2.4 module KB boundaries** - four of six crossed. EXTD (the approved shim and
`apic_wm_top`, a zero-byte label on an existing cell), CTRL (the approved
`mod_need` change makes `cpc_buf` claim-only space; on kern_small the Drivers
page is gated out, no driver loading there), CLONE (kern_small gates streamed
Compress, which needs a truncate kern_small has not got, and XMS staging,
which always finds 0KB there; on both, Compress's state overlays the join's).
**CLONE big is 2 bytes under 9KB and CTRL big 6 under 11KB**, so the next
byte in either gives a KB back. FORMAT (112 short), FDLG (140) and DOCK (198)
stop where only resident cost reaches.

**3, resident crumbs** - the table-indexed thunk was REFUSED on measurement: a
shared indexed body that hands a slot to a far call with every register
intact is ~40 bytes against stubs of 10-14. What paid instead was moving the
thunks to the module side - FILECP's 34 `xf_` and FDLG's 9 `xd_`, as
`call fcpx_go / dw <name>` returning through a zero-byte `cold_kretf` label -
which is most of kern_small's -193. `hb_wakep` was dead (written, never read).
The CZ-hint item was a **real defect**: `drv_find` sized every driver and
module claim from the directory hint and read a missing hint as "not
compressed", so a compressed `.DRV` copied onto the system disk by a host tool
was claimed at its packed size and refused FERR_BIG. `dskw_czknow` sniffs when
the hint is absent (SPEC.md 20.14.6.4), and the file dialog reports the
unpacked size for such a file through the same helper (SPEC.md 38.13.2).

**3, package-side helpers** - `net_find` with the `NETV_STATE` check is an
opt-in (`OS88SOCK_STATE`), -46 bytes across four packages: the saving is
15 x callers - 10, not 15 a package. FTPD stays out because the extra query
pumps ETHER's receive ring at PASV. **The `OSAPI_FILE_FIND` helper is
REFUSED**: across 30 call sites the ES swap is two instructions and usually
needed for other work, so a shared helper loses everywhere but MIDIRack, which
got a local one (-16).

## Defects found and fixed

- A compressed driver with no directory size hint never loaded (above). Row
  `lzdrv-nohint`, red on `f3f894c`.
- `tests/lptlink/partner.py` mirrored `NET_SOCKS = 4` against the driver's 8,
  invisible to `t_mirror`, so `tests/socktest.py` failed on the base. It and
  `linksim.py` now read every shared constant out of the asm
  (`tests/lptlink/asmequ.py`). socktest's "handle came back" assertion
  compared a count taken before the handle existed and could not fail; it now
  goes red with NETV_CLOSE removed. Registered as soak row `socktest`.
- The FORMAT stub change first broke the swap prompt when FORMAT.DRV is
  absent (`fm_c_format` needs `dskw_fmt_load`'s plain CF); found by driving it
  and fixed before merge.
- CLONE's `clo_geom` relied on CX surviving `int 13h AH=08h`; introduced and
  fixed inside the item.
- `lzbig`'s test file stopped forcing the streamed path once CLONE's claim
  shrank (the whole-file path fit); resized 250 → 270KB. Any heap saving would
  have tripped it.

## Not taken, and why

| | cost | buys |
|---|---|---|
| FORMAT under 1KB | ~126 resident (templates + far `dsk_rd1`/`wr1`) | 1KB only while formatting |
| DOCK's 7 `dkk_` stubs as `cw_` shims | ~+28 resident | -63 image, faster paint |
| `MOD_BSS` zeroing on kern_big | ~+20 `.cold` | DOCK's ~45 bytes of state off the image |
| `OSAPI` size-by-name slot | a slot + body | ~40-60 bytes in each of 5 packages |
| SAVER to 10KB | -199 more needed | 1KB |
| HDDTOOL to 16KB | -611 more needed | 1KB (round 1's "10 bytes under" was XMEM's) |

## Coverage

Each item ran its own rows. The merged tree built on all three kernels, ran
the fast tier (56/56), and ran the union of every item's rows through
`tools/os88soak.py`: **175 rows, 173 ok, 1 skipped** (`czdos`, no dosbox on
the box), **1 failed**: the new `fmtreach` row - arm A's verdict and its
volume check passed and the read of the flushed image's last sector came back
empty. **Not load and not a race**, although it looked like both: MartyPC's
flush goes through fluxfox's raw writer, which sizes the file from the BPB it
parsed at MOUNT and never refreshes. The fixture was random bytes, so ~6 times
in 256 its media byte is one fluxfox knows - `FD` writes a 360KB file of the
720KB volume the guest just formatted (the empty read), `F0` reads 18-sector
tracks off 9-sector ones (the DataError seen while writing the row). Both
reproduce every time with the byte forced. `fmtreach` and `fmtlow` now zero the
fixture's BPB and check the flushed file's size first; 6-wide plus two
`fmtlow` all green. The writer's stale BPB is upstream fluxfox's.
Driven by hand where no row reaches: SAVER's settings window (MartyPC CGA),
RAMPAGE's Cancel-then-close (argued, not run).
