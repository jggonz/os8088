# Kernel size pass 11 - drivers: EMS.DRV and HDD.DRV's incoming code

Branch `ksp11-drivers`, cut from `elendilon` at `2a05e31f`. The concept is
the LOADABLE driver code that came in since the squash and is resident
whenever its driver is mounted: all of `EMS.DRV` (SPEC.md 107, new since the
squash, so every verb and table in it was free to change) and `HDD.DRV`'s
incoming changes (SPEC.md 52.1.1 - 52.1.3: rung 1 takes a drive the BIOS also
knows, `OSAPI_VOL_TAKE`, `rep insw`, the seconds-long wait).

**A driver is claimed in whole KB** (`drv_load` claims `ceil(image / 1024)`,
`DRVR_KB`; `os88drv.py` strips trailing zeros only down to that rung), so a
driver byte buys RAM only when a KB boundary is crossed. Bytes are still the
currency - every figure below is the assembled image - and the KB claim is
quoted beside it.

## Base and tip

| | base `2a05e31f` | tip | delta |
|---|---:|---:|---:|
| `EMS.DRV` image (`build/ems.bin`) | 1,208 | 702 | **-506** |
| `EMS.DRV` claim | 2 KB | **1 KB** | **-1 KB** |
| `EMS.DRV` file (LZ4) | 815 | 670 | -145 |
| `HDD.DRV` image (`build/hdd.bin`) | 3,764 | 3,584 | **-180** |
| `HDD.DRV` claim | 4 KB | 4 KB | 0 |
| `HDD.DRV` file (LZ4) | 3,276 | 3,131 | -145 |
| `HDDTOOL.DRV` image | 16,995 | 16,995 | 0 (`hd_kvol` -8 lands in its own `align 512` pad) |
| `HDDTOOL.DRV` file | 13,752 | 13,735 | -17 |

**HDD.DRV's image is at its floor.** It is the resident (everything before
`hd_mbr`), padded to a 512 boundary, then `hd_mbr`'s 512 bytes - laid over
the attach/READY-only code (SPEC.md 52.13.6). So the image is
`3,072 + max(512, run)` while the resident stays under 3,072: the incoming
52.1.1 code had pushed the run 180 bytes past 512, and that 180 is what came
back. A resident byte saved below that only widens the padding - slack for
the next feature, not image - and 3 KB would need the resident under 2,560,
which is ~280 bytes away. Base: resident 2,990, padding 82, run 692. Tip:
resident 2,843 + 222 of attach-only routines moved into the padding, 8 bytes
of padding left, run 510.

**The kernel: 0 bytes on both builds from this branch's own work** (one
constant's VALUE, `DRVM_IMG_EMS` 2 -> 1, in `kernel/driver.inc`'s table:
`tests/unit/t_drvmem.py` checks it against the built image) - measured at
`bacea172`, before the merge, kern_big text 43,428 / bss 5,190 / cold
37,943 exactly the base. After merging `ksp11-voltake` (`e08873ce`) the tip
is voltake's kernel: kern_big text 43,411, bss 5,154, cold 37,941, lowbss
5,598, vgabuf 336 = 92,440 resident; kern_small text 32,016, bss 3,069, cold
23,049, lowbss 2,868 = 61,002.

## TAKEN

### EMS.DRV (all of it new since the squash)

| item | bytes |
|---|---:|
| `em_pown`, the 256-byte per-page owner table, DELETED: a handle is one contiguous run, so first fit (`em_alloc`) asks the eight handles - the run's END starts at the request, and a live handle that overlaps it moves the start to that handle's end and restarts. Proved equal to the page scan in a Python model, 20,000 random alloc/free sequences on boards of 4..256 pages. CAPS's free count is the board less every handle's length | -256 state, and `em_drop`'s page loop |
| handle tables 1..8 indexed `2 x (h - 1)`, the unused slot 0 gone; base (a page fits a byte) and owner packed in one word, `em_hbo` | -9 state |
| owners kept as `slot XOR 0FEh`, so 0 is "nobody": no quarter init at attach, a free test is `cmp x, 1` / `rcl` | -9 attach, smaller tests |
| THE SERVICE TABLE IS ONE CELL: after voltake's merge DRVC_EMS keeps no table copy and `drv_publish` reads `[SI + DSV_PKGCALL]` alone, so `em_svc` is an `equ` 34 bytes ahead of the door word and those bytes are code. (Before that merge the same 34 bytes were the handle tables, zero at attach; this version also drops the 2-byte pad that layout needed and lets the tables strip off the disk as state) | -36 |
| DI saved once in `em_pkg`, the eight per-verb `push di`/`pop di` pairs gone; the verb's CF carried across `popf` in BH's low bit (`rcl`/`rcr`) where `jc`/`popf`/`clc`/`jmp`/`popf`/`stc` was; an unknown verb is a table cell (`em_bad`) | ~-25 |
| attach inlined (one caller each for `em_find`, `em_try`, `em_size`); the frames are `ES - 1000h` from F000h rather than a table; the bases walked by `lodsw`; quarter 0's and 1's probe bytes through `[es:di]`/`[es:bx]` (DI = 0, BX = 4000h, BX banked) instead of `[es:0]`/`[es:4000h]`; `em_ok260` became one store over the list's 260h | ~-60 |
| `em_hnd` falls into `em_bad`, which every verb's refusal shares; `em_free`/`em_gone` share `em_zap`; GONE falls into UNFRAME's walk with every quarter asked | ~-40 |

Every verb keeps every register it kept before except one: FREE's AX was
the handle on success and is 0 now (no caller reads it: `apps/video`'s two
FREE sites and `tests/emstest`'s one). MAP still keeps CX and DX, which
`vp_emap` steps through.

### HDD.DRV

| item | bytes |
|---|---:|
| `HDD_BIOS` is the int 13h drive on EVERY row the ROM reads - a BIOS row's too (`hd_at_bios` +3) - and rung 0's `hd_chs_regs` and reset ask it rather than `HDD_UNIT`. So `hd_kvol` and `hd_geom` are one load and a zero test (no kind test), and the pairing at attach parks the IDE unit in the BIOS row's `HDD_UNIT` (port in `HDD_BASE`) with the BIOS read still finding its drive. `HD_ABI_VER` 4 -> 5 (a version-4 tool's `hd_kvol` would not know a BIOS row by it). `tests/hdtake.py --blank` asserts the restored row is unit 80h, `HDD_BIOS` 80h, base 0 | `hd_kvol` -16 (both images), `hd_geom` -10, attach -1 |
| `hd_twins` rewritten on that: no field put back before the BIOS read, the IDE read finds unit and port in place, CX = 1 kept from the claim for both reads, one restore tail | 164 -> **123** |
| `hd_twins` and `hd_at_geom` MOVED into the padding before `hd_mbr` (the slot `hd_at_dup`/`hd_at_new` were moved to for the same reason); `hd_at_bios`'s `jmp short` to `hd_at_geom` becomes near (+1) | run -142 |
| `hd_at_ident` reads IDENTIFY's seven words with `rep insw` (the rung is CPU_286-only, as `hd_xfer_ide`'s read already relies on) | run -2 |
| `hd_mount`'s take: `[hd_take]` gone. `hd_mount_one` asks `hd_kvol` itself (CF = 0 only on a drive rung 1 took - hd_mount skips any other), and `hd_mount` skips a kernel volume unless the row is IDE; DI is the device row through the whole loop (every callee keeps it). The take's tail shares the add's `inc [hd_nmnt]` | ~-23 resident, 1 state byte |
| `hd_ide_wait`/`hd_ide_drq`: ONE tail. Every way in arrives with CF = 0 (off a `test` or an unborrowed compare), so failure is `cmc` and ready is `pop`/`pop`/`ret` - one instruction (`clc`) LESS on the per-sector DRQ path; drq jumps into wait's tail | -10 |
| `hd_svc` (the tool's door, pre-squash code): a jump table where twelve compares were, nine of whose `je`s were relaxed 5-byte pairs | -33 |
| `hd_xfer_ide`'s relaxed `ja .fail` -> `ja hd_xfer_fail4` (short, backwards; per command) | -3 |

Hot path: the rung-1 sector loop (`.sector` .. `jnz .sector`) is unchanged
instruction for instruction except `hd_ide_drq`'s exit, which loses the
`clc` (2 clocks on a 286, 2 on an 8088) - `rep insw` and the write loop are
untouched.

Behaviour change outside the hot path: `hd_svc_mount`/HSV_VOLOF (the
tool's mount) on a partition the KERNEL already carries now hands it over
(the take) where it used to add a second volume on the same sectors; the
installer refuses that partition as a target (52.10.4.1), so no path reaches
it today.

## REFUSED

* `hd_xfer_ide`'s relaxed `jz .run` (3 bytes): 13 bytes of the command
  prologue would have to go (two `lea`s, a word `mov` of the cylinder, the
  head test reordered, the read/write opcode computed) to bring it in range -
  per-command code in the transfer, all of it bytes the padding would just
  absorb. Not worth the risk at a zero image delta.
* Moving `hd_services` (34) into the padding too: it does not fit the 8
  bytes left, and the run is already under 512.

## Defects found

None. (The relaxed jumps in `hd_svc` and `hd_xfer_ide` are size, not
behaviour.)

## Merge with ksp11-voltake

`OSAPI_VOL_TAKE` is retired: a take is `OSAPI_VOL_ADD` with DX != 0, DL the
index `OSAPI_VOL_AT` answered. On this branch that made the take and the add
ONE call in `hd_mount_one` - DX = 0 or DL = `hd_kvol`'s index, `hd_kvol`
asked once, and the take skipping `OSAPI_VOL_MOUNT` (DX is preserved by the
slot: `dsk_vol_add` banks it and the take arm never writes it). The
coordinator's second note - the kernel reads only `[SI + DSV_PKGCALL]` of
EMS.DRV's table now - is taken above (-36).

## Cross-file list (for the coordinator)

* `kernel/driver.inc` `DRVM_IMG_EMS equ 2` -> `1` (voltake's file, a value
  in `drv_memk`'s table, no size change): the image is 1 KB now and
  `t_drvmem` fails without it. Already on this branch; merged cleanly.
* Contract relied on in voltake's routine: `OSAPI_VOL_ADD` preserves DX on
  both arms (`hd_mount_one` tests it after the call to skip the mount).

## Rows run

* `ems`, `emspico`, `videms`, `videmshyb` - ok (MartyPC, 4/4).
* `hdtake`, `hdtakeboot`, `hdtakeblank` (QEMU, rung 1), `hdboot`, `hdsize`,
  `hdmap`, `hddcp`, `hdnoclaim`, `cpnameshdd` - 9/9 ok.
* `tools/stkbalance.py` over hdd.asm, hdcom.inc, mount.inc, hdtool.inc, ems.asm: 0 unbalanced at base and tip.
* `instdeep`, `instassoc`, `hibernate`, `hibernatedrv`, `viddisk`,
  `viddiskcpu` - 6/6 ok.
* After the voltake merge and the one-cell table: `hdtake`, `hdtakeboot`,
  `hdtakeblank`, `hdboot`, `hddcp`, `ems`, `videms` - 7/7 ok.
* Gates: `make -j2` (fast 61/61), `make -j2 small`, `checkdocs` - green at
  the tip.
