# HDD.DRV's kernel-facing half beyond the entry: hddabi, hdcom, mount, hdsvc, hdxms

Gap review, 2026-11-08. It covers what `drivers.md` and `boot.md` left
unread in the hard-disk driver: the resident/tool contract, the code both
images assemble, partition mounting, the tool's side of the crossing, and the
read straight into XMS. It also covers the kernel half those depend on
(`osapi_vol_add`'s take, `osapi_vol_at`, BPB rule 13, `dsk_xfer`'s driver
arm). Byte figures come from `build/hdd.hma.map` and the sizes in `build/`
(`hdd.bin` 4,096, `hdd.drv` 3,586, `hddtool.bin` 16,510, `hddtool.drv`
12,263). Instruction encodings were counted by hand. Nothing was assembled or
booted.

**These bytes are not kernel-resident.** HDD.DRV is a driver image. It sits in
the HMA (`DRV_HMA_ORG` = C000h, Makefile:4545) or in a 4KB heap claim, from
boot to power-off, on every hard-disk machine. The image is exactly at its
rung. The resident runs 0x000-0xDFC, `hd_mbrok` is at 0xDFD, and 2 bytes of
`align 512` pad come before `hd_mbr` at 0xE00. The once-run in `hd_mbr`'s
window ends at 0xFFC. **Any growth beyond 2 resident bytes makes the claim
4,608.** Every addition below is costed with that in mind, and the savings
are listed so that they can pay for the additions.

## Scope read

| file | lines | what |
|---|---|---|
| `drivers/hdd/hddabi.inc` | 1-317, all | the constants, `HDT_*`, `HSV_*`, `HDM_*`, the request block |
| `drivers/hdd/hdcom.inc` | 1-626, all | device table, `hd_chs`, `hd_part_load`/`_blank`/`_ent`/`_used`/`_big`/`_isfat`, `hd_kvol`, `hd_geom_ok`, the tool-only text helpers |
| `drivers/hdd/mount.inc` | 1-560, all | `hd_mount`, `hd_mount_one`, `hd_cylprobe`, `hd_vol_*`, `hd_unmount*`, `hd_raw` and its bounce |
| `drivers/hdd/hdsvc.inc` | 1-330, all | the tool's `hd_raw` thunk, `hd_sync`, the same-named wrappers |
| `drivers/hdd/hdxms.inc` | 1-216, all | `hd_pkg`, `hd_xa_arm`/`_dis`/`_a20`/`_read` |
| `drivers/hdd/hdsec.inc` | 1-47, all | the sector buffers and the `hd_mbrok` placement |
| `drivers/hdd/hdtool.inc` | 1-721, all | tool loader, page thunks, `hd_svc` and its 14 handlers |
| `drivers/hdd/cfg.inc` | 1-303 (the code) | `hd_cfg_mark`, `hd_cfg_automount` |
| `drivers/hdd/hdd.asm` | 1-1599, all | including the header, the rung-1 PIO loop, `hd_bios_run`, the IDE helpers, data, `hd_at_*`, `hd_twins`, `hd_attach`, `hd_ready` |
| `drivers/hdd/cppage.inc` | 625-700 | the geometry editor's own `hd_geom_ok` gate |
| `drivers/hdd/ipkg.inc` | 1-120 | `HDT_PKG` / `HDI_*` |
| `kernel/disk.inc` | 205-235, 280-300, 1600-1700, 2289-2340, 2756-2870, 3300-3380, 3640-3870, 5070-5300 | `DVOL_MAX`, `DV_SECS`, `dsk_xfer`'s `.drv` arm and `.drvcut`, `dsk_boot_from`, `osapi_vol_at`, the fence, `osapi_vol_add` (take), `vol_del`/`vol_mount`, BPB rules 13-15 |
| `kernel/drvvol.inc` | 1-272, all | the bank pair (`diskread.md` already holds its mismatches 41-44) |
| `kernel/driver.inc` | 2310-2440, 3840-3880 | `drv_nostamp`, `drv_call`, DRVV_READY's send |
| `kernel/kernel.asm` | 6640-6760 | `osapi_file_here`/`_goto`/`_goto_q` |
| `kernel/diskw.inc` | 1800-1830, 4955-4982 | multi-sector data and FAT writes |
| `apps/os88api.inc` | 3480-3600 | the door, `HDV_*`, `HDI_*` |
| `apps/os88ub.inc` | 1-60 | `ub_set`'s contract |
| `apps/video/video.asm` | 7475-7530 | the one `HDV_ARM` caller |
| `SPEC.md` | 3805-3856, 51.8.1 (102777-102830), 52.1.4 (103336-103376), 52.11-52.11.3, 52.12, 52.13, 52.13.6, 117.4 | the contracts |
| `docs/plans/ABI-BREAK-LIST.md`, `KERNEL-BYTE-QUEUE.md` | grep | **P2** (`HD_ABI_VER` is private and moves freely), **L3** (`mega_gpfix` serves HDD<->HDDTOOL), **C3**/**C4**. The queue has no HDD row |

Map spans (HMA copy, offsets from C000h):

| range | bytes | what |
|---|---|---|
| `hd_svc` .. `hd_mount` (C476-C5E4) | **366** | the 14-entry `HSV_*` dispatcher and handlers, which only the tool ever calls |
| `hd_vol_of` (C768-C77C) | 20 | tool-only too (`HSV_MOUNT`, `HSV_VOLOF`) |
| `hd_mount` .. `hd_raw` (C5E4-C77C) | 408 | mounting and the volume table walks |
| `hd_raw` (C77C-C7ED) | 113 | the bounce included |
| `hd_blk` .. `hd_buf_step` (C81C-CA6A) | 590 | DSV_BLK, GEOM and both rungs |
| `hd_pkg` .. `ub_set` + `UB_DATA` (CC79-CDFD) | 388 | the door and the XMS read, sitting in what was `hd_mbr`'s pad |
| `hd_attach` .. `hd_mbr_end` (CE00-CFFC) | 508 | the once-run, in the partition buffer's 512 bytes |

## Verdict

The kernel-facing contract is sound and better defended than it first
appears.

- **One validator for every BPB.** A partition's BPB is checked by the
  kernel's own `disk_mount` rules 3-16. Past 32MB the volume's size is
  declared "unknown" (`HDV_SECS` = 0, mount.inc:147-151), and **`hd_blk`'s
  `HDV_LEN` fence** (hdd.asm:200-210) then holds the partition's real end.
  That fence is the one place that knows it, and it bounds every DSV_BLK.
- **Base + offset cannot wrap.** `hd_part_big` holds a length under 2^22 and
  `hd_chs` refuses any LBA past cylinder 1,023 (under 2^24), so a hostile
  base can never alias the MBR.
- **The take is atomic.** `osapi_vol_add` hands the BIOS boot row to the
  driver by one word store of kind and unit (disk.inc:3747-3780), and
  `osapi_vol_at` names that row by `DV_BUNIT` afterwards.
- **The system-volume bank is closed.** DRVV_READY goes through
  `%1_call` → `drv_call` (driver.inc:3864). `drv_call` clears the instance
  stamp (`drv_nostamp`, 2338), so `OSAPI_FILE_HERE` at hdd.asm:1414 reads the
  machine.

What needs work is at the **edges of trust**, and mostly at the doors added
since the split:

- `HDV_ARM` takes any segment (hdk-1).
- `HDV_TOOL` turned the package door into a door to a partition erase, and
  five documents still say "no sector" (hdk-2).
- The resident accepts `HSV_MOUNT` without the duplicate guard `hd_mount`
  has (hdk-7).

On the transport, two gaps:

- **Rung 0 gives up on a sector its controller corrected** (status 11h,
  hdk-3). That is the owner's MFM class of machine.
- **Rung 1 never retries anything** (hdk-4).

The partition table is checked by type and length only (hdk-5, hdk-6).

Size: about **386 resident bytes serve only the tool**, and the state-copy
half of them could go (hdk-11). Three small savings are free (hdk-8, hdk-9,
hdk-10).

Answers to the brief's questions:

- **Is the mount path validated as hostile input like the floppy's?** The
  BPB is, by the same code. The partition table is not: there is no check on
  the status byte, overlap, duplicate base or end-past-disk (hdk-5).
  CHS/LBA overflow is safe, as argued above.
- **Is the DRVV_READY bank closed?** Yes. One step remains, which is to stop
  depending on it (hdk-10).
- **Does hdxms honour A20, IF and the XMEM contract?** A20 is probed at
  every arm (hdxms.inc:87). IF is off per sector (`pushf`/`cli`,
  hdxms.inc:192). `ub_set` masks NMI. No tier-2 path calls the BIOS block
  move. The non-386 and PM fallbacks refuse with AX = 1, and the one caller
  remembers that "for good" (video.asm:7495). **What it does not do is fence
  the segment it arms** (hdk-1).
- **Are the shared names in sync?** Yes for `hd_raw`, `hd_geom_push` and
  `hd_cfg_mark`. **`hd_mount_one` is two different contracts under one
  name.** The resident's takes SI = entry, [hd_sel] and answers in rows. The
  tool's takes AL = device and answers in `[hd_mvol]`. That is deliberate and
  documented at hdsvc.inc:140, but the resident's header is wrong about its
  own CF (hdk-8).
- **How do retries and error codes compare with the floppy's `dsk_xfer`?**
  Rung 0 makes three attempts with AH=00h between them. Rung 1 makes one and
  stops. Every rung-1 failure but the end-of-command ERR/DWF maps to 04h. The
  kernel distinguishes only 03h (`diskw.inc:5039`), so the mapping is
  cosmetic. The missing retry and the 11h are not cosmetic.

## Strengths

1. **`hd_blk` is the one fence that knows where a partition ends**
   (hdd.asm:200-210). It compares the transfer's 32-bit END with `HDV_LEN`
   before adding the base. That closes the "unknown" hole that rule 13 leaves
   past 32MB (disk.inc:5219-5222). It also backs up rule 13 below 32MB, which
   trusts `DV_SECS` = the partition's own table entry.
2. **HMB_BAD is not HMB_NONE** (hdcom.inc:165-223). A failed LBA 0 read and
   a missing signature lead to the same blank buffer, but only the second
   lets a commit go ahead. This is the distinction that prevents the "marginal
   LBA 0 formats through three partitions" loss its comment describes.
3. **The twin proof is conservative in the direction that keeps disks**
   (hdd.asm:906-1114). The key is geometry and the proof is LBA 0 read
   through both rungs with a 55AA check. If more than one BIOS row matches,
   the drive pairs with none.

   This is Windows 3.1's 32-bit disk access (FastDisk/WDCTRL) for the same
   reason: take the controller from the BIOS so interrupts stay on. Here the
   take only happens once both rungs agree byte for byte.
4. **The once-run code is the partition buffer** (hdd.asm:1120-1590,
   SPEC.md 52.13.6). Assembly asserts keep `hd_cfgbuf` (34 bytes) clear of
   `hd_ready`, and the run fits its 512-byte window. That is 508 bytes of
   attach and ready code costing nothing after DRVV_READY.
5. **Every index crossing the seam is range-checked against the resident's
   table** (`hd_dev_row_ck`, hdcom.inc:103). `HSV_MSG` range-checks its code
   (hdtool.inc:646-652). `hd_tool_need` validates the image that actually
   ARRIVED: magic, format, link base, dispatcher bytes and `HD_ABI_VER`
   (hdtool.inc:165-180). The tool is treated as hostile input even though it
   ships on the same disk.
6. **`hd_ide_wait` is bounded in seconds and a floating bus is answered at
   once** (hdd.asm:692-715). This is the Linux pre-5.11 hang it names, and it
   is avoided.
7. **A geometry does not travel; a mount preference does** (hdd.asm:1450-1474,
   FIELD-NOTES 33). The probe wins unless the record says the user typed it.
8. **The XMS read keeps exact accounting** (hdxms.inc:170-211). Sectors that
   reach the buffer by any other road leave their map bit clear. The range
   test is a signed segment difference plus a whole-sector test, so a buffer
   below the arm reads as huge and is refused.

## Findings

### hdk-1 (hazard, medium) `HDV_ARM` redirects ANY rung-1 read into the armed segment, including the kernel's own

`hd_xa_arm` checks the CPU, the length and A20 (hdxms.inc:72-106). It never
asks whether **DX**, the buffer's segment, belongs to the caller. Two things
follow from that:

- `hd_xfer_ide`'s `.read` asks `hd_xa_read` about **every** rung-1 read
  (hdd.asm:489-492).
- The kernel's FAT window (`dsk_fatseg`), directory sectors, `dsk_secbuf`
  and the sector cache are rung-1 reads like any other.

**Failure scenario.** A package arms with a wrong DX, or DX left over from
another routine, that covers the FAT window's segment. Then:

1. The next FAT-window load reads into XMS.
2. The kernel's window keeps the PREVIOUS window's bytes and `dsk_fatw0` names
   the new sector range.
3. The next `dskw_flush` writes those stale bytes back as the new range's FAT
   (diskw.inc:4955-4982).

The result is silent cross-linked clusters on C:. hdxms.inc:34-36 rests the
door's safety on "WRITES ARE NEVER REDIRECTED". **But a redirected read of
metadata becomes a wrong write one flush later.**

A second leak: an arm outlives its owner. Nothing disarms on instance
teardown. If a package faults between ARM and DISARM, `[hd_xa_on]` stays 1
with a freed segment, and the next claim placed there loses its rung-1 reads.

The one caller (`vp_harm`, video.asm:7475-7505) passes the right DX, so
**no in-tree defect is known.** The door, though, is reachable by any `.O88`.

**Recommendation (now).**

- Fence DX against the caller: `mov ax, es / cmp dx, ax / jb .no` refuses a
  segment below the caller's own segment, which covers every kernel segment.
  That is **+6 bytes**, and it needs checking that the Video Player's chunk
  sits above its own segment.
- Clear `[hd_xa_on]` in `hd_detach` (it is already zeroed by a fresh image,
  so this matters only on a future re-attach).

**Record.** The exact form: the kernel tags its DSV_BLK reads that are user
DATA reads (a spare bit in AL alongside 0/1/2), and the driver redirects only
those. That moves the "this buffer is the package's" fact to the one party
that knows it. It is a DSV_BLK contract change (`DRV_VER`), so it belongs on
the flag-day list.

### hdk-2 (consistency, medium) The package door reaches a partition ERASE now, and five places still argue it reaches no sector

`HDV_TOOL` (hdxms.inc:58-61) forwards to `HDT_PKG`. Its `HDI_GO` with
CH bit 0 = 1 erases and reformats a partition, writing the MBR, the FATs and
the boot record through `hd_raw` (os88api.inc:3588, SPEC.md 117.4). Any
`.O88` can call it through `OSAPI_DRV_CALL`/`DRVU_CALL`. The real fence is
the engine's `hd_iact_ok`, which refuses the live volume and keeps the user's
choice in the wizard. It is **no longer the door's verb list.**

Yet these all still state the old argument:

- hdxms.inc:34-36 ("reaches two verbs, neither of them a sector")
- hdd.asm:1569-1572 ("THE DOOR, and it reaches two verbs")
- os88api.inc:3490-3493 ("HDD.DRV's reaches HDV_ARM and HDV_DISARM and
  nothing else ... arrive at a raw sector write")
- os88api.inc:3550 ("TWO verbs and no sector")
- SPEC.md 103347 ("the door reaches two verbs and no sector")

SPEC.md 117.4's "and still no sector" is literally true (no verb takes an
LBA), but it is now the wrong safety claim.

**Recommendation (now).** Rewrite the five, as listed under the mismatches
below. Then decide explicitly whether an erase reachable from any package
without a tool-owned confirm is acceptable. The cheapest strengthening is in
the TOOL, not the resident: `HDI_GO` with erase refuses unless the
wizard-confirm token from `HDI_SLOTS` is echoed. That is ~+10 tool bytes and
0 resident bytes.

### hdk-3 (weakness, medium, PLAUSIBLE: needs the field machine) Rung 0 fails a read its controller CORRECTED (int 13h status 11h)

`hd_xfer`'s rung 0 (hdd.asm:337-364) treats every CF = 1 as an error: reset,
retry, and after three attempts fail. **Status 11h is "ECC corrected data
error".** The XT and AT fixed-disk BIOSes set CF and return 11h with the data
in the buffer already corrected. DOS's fixed-disk path counts that as a
success. On an MFM drive behind an ST-11M or a WD1002 (the owner's 5150 class:
docs/FIELD-MACHINES.md), a weak sector that the controller corrects on every
read is unreadable through HDD.DRV and readable under DOS.

The kernel's own BIOS path for an un-taken boot partition has the same blind
spot: no `0x11` appears anywhere in disk.inc.

**Recommendation (now).** On AH = 11h:

- a one-sector run is accepted as done (`cmp ah, 11h / je .next`, +5 bytes);
- a longer run is re-issued one sector at a time so that the corrected sector
  is accepted alone, because an XT controller may stop the command at the
  corrected sector. That is ~+12 bytes in all.

The kernel side is a separate ~6-byte row in the BIOS retry loop, raised with
`diskread.md`. Fund it from hdk-8/9/10.

### hdk-4 (weakness, low) Rung 1 has no retry and no reset

`hd_xfer_ide` jumps to `hd_xfer_fail4` (hdd.asm:442, 521-522) on any of:

- a select timeout;
- a DRQ wait that saw ERR or timed out;
- an end-of-command ERR/DWF, which answers 0Ah.

There is no second attempt. Rung 0 gets three attempts with a controller
reset between them, and the floppy ladder gets more (diskread dr-3). One
transient on an aging IDE drive is a failed file operation, and a "Disk
error" during an install.

The retry costs little here because `hd_buf_step` has already left `hd_lba`
and `[hd_bofs]` on the failing sector. A retry is a re-entry at `.run` with
a counter, ~+10 bytes. A soft reset (SRST through the device-control port,
base + 206h) between attempts is ~+12 more, plus a re-issue of
`hd_ide_setparams`, since SRST forgets the geometry.

**Prior art:** Linux's ATA error handler resets and then retries; DOS
retries and then asks Abort/Retry/Ignore.

**Recommendation (now):** the bare retry. The reset is optional.

### hdk-5 (hazard, low) The partition table is trusted by type and length alone

Mounting a slot requires `hd_part_isfat` (a FAT type with a non-zero length)
and `hd_part_big` (under the FAT16 ceiling) (hdcom.inc:331-399). There are
three gaps.

1. **The status byte is never checked.** LBA 0 of a "superfloppy" (FAT
   straight at LBA 0, as some CF cards and host-formatted disks are) has
   55AA, so it reads as `HMB_OK` and its boot code at 446-509 becomes four
   garbage entries. Most fail on read. A plausible one is shown to the user,
   who can Delete or Format it, and that commit writes a table over a live
   filesystem.

   Linux's `msdos_partition` refuses a table whose boot indicators are not
   00h/80h for exactly this case.

   **Recommendation (now):** in `hd_part_load`, treat any entry whose status
   is not 00h or 80h (`test byte [si+HP_STATUS], 7Fh`) as `HMB_BAD`, so the
   table is never mounted and never committed over. About **+14 bytes in
   hdcom.inc, so in both images.**
2. **Two slots with the same base, or overlapping extents, both mount.**
   `hd_vol_find` keys on (device, slot) (mount.inc:359-376), so a table with
   a duplicated entry gives two volumes, two icons and two FAT caches over
   the same sectors. DOS does not check this either, but DOS mounts one
   primary per disk.

   **Recommendation (now):** in `hd_mount_one`, refuse a base equal to a live
   row's `HDV_BASE` on the same device (~+20 bytes). This also covers hdk-7.
3. **A partition that runs past the disk** is not refused at mount. See
   hdk-6.

### hdk-6 (bug, low) `hd_chs` bounds at cylinder 1,024, not at the drive; its comment says "past the drive"

hdcom.inc:117 says "CF = 1 = past the drive", but :142 is `cmp ax, 1024`. An
LBA between the drive's last cylinder and 1,023 is handed to the ROM or the
task file as it is, and what happens then depends on the controller:

- a seek error 40h on most;
- an IDNF from an IDE drive;
- on some XT controllers, a wrapped head positioner.

So a partition entry that overstates its length is refused nowhere until a
write near the volume's end fails.

**Recommendation (now): `cmp ax, [di+HDD_CYL]`.** The encoding is 3 bytes,
the same as the `cmp ax, imm16` it replaces, so **0 bytes in both images**.
It makes the comment true and turns "past the disk" into a clean 04h at the
driver.

One caveat. A rung-1 row TAKEN from the BIOS keeps the BIOS's cylinder count,
which may be one short of IDENTIFY's (hd_at_dup:925-928). A partition written
by this tool on an untaken rung 1 using that last cylinder would then become
unreachable. Either set `HDD_CYL` from IDENTIFY in `hd_twins` when the proof
succeeds, or accept the loss and say so.

### hdk-7 (hazard, low) `HSV_MOUNT` does not refuse a partition that is already mounted

`hd_mount` guards with `hd_vol_find` (already ours) and `hd_kvol` (the
kernel's) before calling `hd_mount_one` (mount.inc:56-72). `hd_svc_mount`
calls `hd_mount_one` directly (hdtool.inc:597-625), and `hd_mount_one`
repeats neither test.

Both in-tree callers unmount the slot first (inst.inc:2789/2837, 2860/2872),
so **nothing misbehaves today.** But the resident owns the volume table and
its rule should not depend on the tool's call order. A tool built against a
future installer that forgets the unmount gives two volumes on one partition.

**Recommendation (now):** move the guard into `hd_mount_one`. hdk-5 (2)'s
base check subsumes it. Then `hd_mount`'s own `hd_vol_find` test can go, for
a net ~+10 bytes.

### hdk-8 (consistency, low; size -2) `hd_mount_one`'s CF is read but not documented, and the read is redundant

The resident `hd_mount_one`'s header says "out: [hd_nmnt] incremented on
success; [hd_msg] set on a failure; clobbers: flags" (mount.inc:110-113). But
`hd_svc_mount` branches on its CF, "hd_mount_one's verdict"
(hdtool.inc:619-620).

On the `.badvol` path the CF that comes back is `OSAPI_VOL_DEL`'s, through
`hd_unmount_row` (mount.inc:175-177). The answer is right only because
`hd_vol_of` runs next, finds no row, and answers 0FFh with CF = 1.

**Recommendation (now):**

- Delete the `jc hd_svc_out` at hdtool.inc:620 (**-2 resident bytes**), so
  that `hd_vol_of` is the whole answer.
- Correct the header to "out: [hd_nmnt] incremented on success, [hd_msgc]
  set on a refusal; CF is not an answer (hd_svc_mount reads hd_vol_of)".

### hdk-9 (size, low; -11) DSV_CPNAME duplicates the name the header already carries

`hd_s_page: db 'Hard Drive', 0` (hdd.asm:838, 11 bytes) is published as
DSV_CPNAME (hdd.asm:1555). `OS88_DRIVER 'Hard Drive', ...` has already placed
the same NUL-padded string at header +16 (os88drv.inc:810-811), in the same
segment.

**Recommendation (now):** `dw DRV_ORG + 16` in the table, and delete the
string. That is **-11 resident bytes** and is correct in the HMA copy too
(`DRV_ORG` is C000h there).

The same holds for RAM DISK (`rd_name`, 9 bytes, ramdisk.asm:149) and
NET (`net_cpname`, 8 bytes, net.asm:224, which DSV_NAME shares), so **-28
bytes across the tree**. A one-line note in os88drv.inc ("the header's name
IS a NUL string at +16; publish it rather than a copy") keeps the next driver
from copying the pattern.

### hdk-10 (rethink, low; -4) Ask `OSAPI_VOL_SYS` for the system volume instead of banking one at DRVV_READY

`hd_ready` banks `OSAPI_FILE_HERE` into `hd_tvol`/`hd_tcwd` (hdd.asm:1414-1416).
`hd_tool_need` goes there (hdtool.inc:128-130). The bank depended on a kernel
fix (`drv_nostamp`, SPEC.md 51.2.3) and on DRVV_READY being sent inside
`drv_load`'s bracket.

`OSAPI_VOL_SYS` (os88api.inc:3282, an inline cell: BL = `[dsk_bootvol]`)
answers the question directly, and the tool's own page already uses it
(cppage.inc:1014). The banked directory is always the root anyway
(`drv_mounted` stands at cluster 0, drvvol.inc:54).

**Recommendation (now).** In `hd_tool_need`, replace `mov dx,[hd_tcwd]` /
`mov bl,[hd_tvol]` (8 bytes) with `xor dx,dx` / `call OSAPI_VOL_SYS`
(7 bytes). Delete the 3 bytes of `hd_tvol`/`hd_tcwd`. The stores in
`hd_ready` sit in `hd_mbr`'s window and are free either way. **-4 resident
bytes**, and the comment at hdtool.inc:16-25 ("a driver has no way to ask
which") stops being stale.

### hdk-11 (rethink, medium; ~-100 resident, ~+40 tool) The seam copies the resident's STATE through six verbs; publish pointers to it instead

Of the 386 tool-only resident bytes:

- `HSV_SYNC` (21 bytes), `HSV_STATE` (43), `HSV_SEL` (13), `HSV_MSG` (13) and
  `HSV_VOLOF` (12), plus `hd_vol_of` (20), exist to copy bytes the resident
  owns into or out of the tool.
- `HSV_PAGE` and (outside INSTBENCH) `HSV_CTRS` are table entries that point
  at `hd_svc_unk`.

SPEC.md 52.11.2 rejected sharing tables because "declaration order [would
be] an unwritten part of the ABI". But `HDD_*` and `HDV_*` are already a
written ABI in hddabi.inc, and `HD_ABI_VER` already refuses a mismatched
pair at every load.

**Proposal.** `HDT_INIT` answers with far pointers to `hd_devs`, `hd_vols`,
`hd_ndev`, `hd_sel` and `hd_msgc`: five words in the request block, each a
separate pointer, so declaration order still means nothing. The tool then:

- reads directly: SYNC, STATE and VOLOF become far reads;
- writes directly where a write has no side effect: SEL and MSG.

Only the verbs that do something keep their handlers: RAW, MOUNT/UNMOUNT
(+DEV), GEOM (it pushes to the drive) and CFGMARK.

Cost: about **-100 resident bytes** against ~+40 tool bytes. It also removes
the `HSV_SYNC`-before-act discipline (hdsvc.inc:101-115), because a pointer
cannot go stale.

kern_mega: the tool already reaches the resident through `mega_gpfix`'s
data-selector conversion (ABI-BREAK-LIST L3). A far pointer handed over at
INIT is the data selector, so this narrows L3's surface rather than widening
it.

**Private ABI, `HD_ABI_VER` 8: phase now** (ABI-BREAK-LIST P2).

### hdk-12 (perf, low; +2) Rung 1 writes a word at a time where it reads with `rep insw`

hdd.asm:503-508 is `mov ax,[es:di] / out dx,ax / inc di / inc di / loop`
(8 bytes, ~20 clocks a word on a 286). `rep outsw` (F3 6F) wants DS:SI, and
SI is the sector count. The fix is:

`push ds / push si / mov si,di / push es / pop ds / rep outsw / pop si / pop ds`

That is 10 bytes, **+2, which fits the 2-byte pad in front of `align 512`
exactly**, and ~5 clocks a word. Every multi-sector write gets faster: data
runs and FAT ranges are multi-sector (diskw.inc:1806-1829, 4974-4979), and
an install writes megabytes.

SPEC.md 52.1.2 measured the read side's ~15%. The write side has never been
measured. Take the number in the final pass on 86Box's mr286 profile, then
decide.

### hdk-13 (hazard, low; +4) `hd_raw`'s bounce copies `CL x 256` words and drops a count's high byte

mount.inc:526-527 builds the copy count as `mov ch, cl / mov cl, al(=0)`,
which is sectors × 256 words modulo 65,536. The comment says "a raw transfer
is one to three sectors, well under 256". But `HSV_RAW` hands `HRQ_CNT`
through from the tool unchecked (hdtool.inc:533-551). A run of 256 sectors or
more into an HMA or selector buffer would move every sector and copy back
fewer.

**Recommendation (now):** in the bounce, refuse CX > 127, the same cap
`hd_bios_run` uses: `cmp cx, 127 / ja .out` with CF set, +4 bytes. Or state
the bound in `HSV_RAW`'s ABI line.

### hdk-14 (consistency, low; record) `hd_buf_step` walks DX on an offset wrap; BLKCAP_SEL promises it walks only BX

SPEC.md 51.8.1 defines `BLKCAP_SEL` as "my DSV_BLK loads DX into a segment
register as it is and walks only BX". `hd_buf_step` (hdd.asm:659-666) does
`add word [hd_bseg], 0x1000` when `hd_bofs` wraps, and on a selector that
arithmetic is meaningless. It is unreachable today, because a selector's run
cannot cross its own 64KB limit, and a run that ends exactly at 0x10000 wraps
only after its last sector. **Record:** a one-line note on `hd_buf_step` and
in 51.8.1 saying why the carry is dead under a selector.

## Rethinks

- **hdk-11, the shared-pointer seam.** It is the one change here that removes
  a category of code rather than trimming it. Six of fourteen `HSV_*`
  handlers exist only because the tool may not see bytes it is assembled to
  understand.
- **hdk-1's record form.** Let the kernel say which DSV_BLK reads are a
  package's data (a flag bit) rather than letting a package name a segment.
  The arm then becomes a property of ONE transfer the kernel already
  separates out (dskw's data path), and the map stays exact.
- **One partition-table validator, in hdcom.inc** (hdk-5/6/7 together). It
  would hold the status bytes, the end against the drive, and duplicates or
  overlap against each other, written once and assembled into both images
  like `hd_part_big` is. The tool's State column (`HIS_ALIEN`) and the
  resident's mount would then refuse the same tables for the same reasons. A
  partition that is wrong is the same wrong whichever image is looking.

## Prior art

| system | what it does | os8088 | verdict |
|---|---|---|---|
| **MS-DOS 3.x IBMBIO** | Fixed-disk requests are retried by the BIOS driver, and an ECC-corrected status is taken as data. Unrecoverable errors reach the user as Abort/Retry/Ignore | rung 0 retries 3x but fails 11h (hdk-3); rung 1 never retries (hdk-4); the kernel has no Retry prompt | adopt the 11h rule and one rung-1 retry |
| **Windows 3.1 WDCTRL (32-bit disk access)** | Takes a WD1003-compatible controller from the BIOS so Windows can run with interrupts enabled. It refuses to load unless the controller and the BIOS geometry check out | `hd_twins` takes an IDE drive from the BIOS for the speaker's IRQ0, proved by LBA 0 through both rungs | os8088 does it with a stronger proof; **already adopted** |
| **Linux `block/partitions/msdos.c`** | Rejects an "MBR" whose boot indicators are not 00h/80h (a FAT boot sector at LBA 0); truncates a partition past the end of the device with a warning | neither check (hdk-5, hdk-6) | adopt the boot-indicator test (+14 bytes) and the cylinder bound (0 bytes) |
| **Linux libata EH** | Soft reset, re-IDENTIFY, retry | none on rung 1 | adopt a retry; SRST optional |
| **OS/2 1.x DevHlp `Lock`/`VirtToPhys`** | A driver that bypasses the copy locks the caller's buffer and is handed its physical address by the kernel | `HDV_ARM` takes a segment the caller names, unverified | the kernel should vouch for the buffer (hdk-1 record) |

## Doc/code mismatches

| # | file:line | current text | code does | proposed |
|---|---|---|---|---|
| 1 | drivers/hdd/hdd.asm:11-16 | "A volume is a small index with 16-bit VOLUME-RELATIVE LBAs ... a partition is capped at 65,535 sectors - 31.99MB" | DSV_BLK takes DI:SI, a 32-bit volume-relative LBA (hdd.asm:181-216), and `hd_part_big` admits up to 4,194,000 sectors (hddabi.inc:131-137) | "A volume is a small index with VOLUME-RELATIVE LBAs, 32 bits since SPEC.md 18.7.5, so the FAT layer ... are the floppy's, unchanged, and a partition is capped at FAT16's own ceiling (HP_MAXHI:HP_MAXLO, 2,047.9MB)." |
| 2 | drivers/hdd/hdd.asm:5-9 | "Everything the user can see is here - the Control Panel's Hard Drive page, the disk tool that partitions and formats, and the Mount button" | the page, the disk tool, the formatter and the installer are HDDTOOL.DRV's (cppage.inc, tool.inc, fmt.inc, inst.inc; SPEC.md 52.11, 52.13). The resident keeps the probe, both rungs, the volume table, mounting and one refusal line | "The transport and the volume table are here; everything the user can see - the Control Panel's Hard Drive page, the disk tool, the formatter and the installer - is HDDTOOL.DRV's, read off the system volume on demand (SPEC.md 52.11, 52.13)." |
| 3 | drivers/hdd/hdd.asm:40-42 | "Sub-modules: part.inc (the partition table and the allocator), fmt.inc (the FAT formatter), tool.inc (the one window both of those are reached through), page.inc (the Control Panel page)." | the resident includes hddabi, hdcom, cfg, hdtool, mount, hdxms, hdsec (hdd.asm:78-82, 1116-1118). part.inc and page.inc do not exist; partw/fmt/tool/inst/cppage/ipkg are the tool's (hddtool.asm) | "Sub-modules: hddabi.inc (the contract with HDDTOOL.DRV), hdcom.inc (code both images assemble), cfg.inc (the settings blob), hdtool.inc (loading and calling the tool), mount.inc (the volume table), hdxms.inc (the read into XMS), hdsec.inc (the buffers)." |
| 4 | drivers/hdd/hdd.asm:886 | "hd_cap MOVED to page.inc" | `hd_cap` is at cppage.inc:1130 | "hd_cap MOVED to cppage.inc" |
| 5 | drivers/hdd/hdd.asm:898-900 | "the image is 3,584 bytes whenever the resident above stays under 3,072 and the run under 512" | `hdd.bin` is 4,096. `hd_mbr` is at offset 0xE00 (hdd.hma.map: CE00), so the resident is 3,584 and the run makes it 4,096 | "the image is 4,096 bytes whenever the resident above stays under 3,584 and the run under 512" |
| 6 | drivers/hdd/hdsec.inc:20-24 | "after a 512-aligned 512-byte buffer it put the resident's image at 5,121 ... the image is exactly 5,120" | the image is 4,096 (`build/hdd.bin`) since SPEC.md 52.13.6 laid the once-code in `hd_mbr` | "...the image is exactly 5,120. (It is 4,096 since SPEC.md 52.13.6, and the byte is still where it costs nothing: in front of the `align 512`.)" |
| 7 | drivers/hdd/hddabi.inc:157-159 | "(DRV_ORG + PKG_DISP: E00Ch from the HMA)" | `DRV_HMA_ORG := 0xC000` (Makefile:4545) | "(DRV_ORG + PKG_DISP: C00Ch from the HMA)" |
| 8 | drivers/hdd/hdtool.inc:191-193 | "assembled at DRV_ORG = E000h there (SPEC.md 2.10.8), so it is E00Ch" | as 7 | "assembled at DRV_ORG = C000h there (SPEC.md 2.10.8), so it is C00Ch" |
| 9 | SPEC.md:3851-3852 | "which from the HMA is DRV_HMA_ORG + PKG_DISP (E00Ch) and not PKG_DISP" | as 7; SPEC.md:3805 itself says 0xC000 | "which from the HMA is DRV_HMA_ORG + PKG_DISP (C00Ch) and not PKG_DISP" |
| 10 | drivers/hdd/hdtool.inc:16-25 | "FINDING THE FILE NEEDS NO NEW API ... a driver has no way to ask which." | `OSAPI_VOL_SYS` (os88api.inc:3282) answers BL = [dsk_bootvol], and the tool's own page uses it (cppage.inc:1014) | "FINDING THE FILE: HDDTOOL.DRV is on the SYSTEM volume ... and OSAPI_VOL_SYS names it. The bank at DRVV_READY predates that cell and is equivalent: drv_load stands on the boot volume's root before DRVV_READY (SPEC.md 51.2.2)." Or, with hdk-10, describe the cell alone |
| 11 | SPEC.md:106197-106203 (52.11) | "Finding the file needs no new API ... a driver cannot ask which." | as 10 | "Finding the file needs no new API: `OSAPI_VOL_SYS` names the system volume, and before it existed `hd_ready` banked `OSAPI_FILE_HERE` from inside `drv_load`'s bracket, which answers the same." |
| 12 | SPEC.md:106213-106217 (52.11.1) | "the resident→tool vocabulary is **four verbs** ... carried in `BP` through the dispatcher — `BP` is a verb rather than a proc offset" | BP = the tool's ENTRY OFFSET from its header +6, the verb rides AL (hddabi.inc:139-147, hdtool.inc:269-277), and there are ten `HDT_*` numbers, 0-10 | "...was four verbs (`HDT_INIT`, `HDT_FORMAT`, `HDT_INSTALL`, `HDT_SHUT`), eleven numbers now with `HDT_BUSY`, the five page verbs and `HDT_PKG`, carried in **AL**; BP is the tool's entry offset, out of its own header at +6, because the dispatcher is `call bp`." |
| 13 | SPEC.md:106230 (52.11.1) | "**twelve verbs the other way** (`HSV_*`)" | `HSV_*` is 0-13; 12 implemented, `HSV_PAGE` unimplemented, `HSV_CTRS` INSTBENCH only (hdtool.inc:514-525) | "**fourteen numbers the other way** (`HSV_*`), twelve of them implemented in every build" |
| 14 | drivers/hdd/hdxms.inc:34-36 | "DSV_PKGCALL reaches two verbs, neither of them a sector, so no package can reach DSV_BLK through it" | `hd_pkg` reaches `HDV_TOOL` too (hdxms.inc:58-61), and `HDI_GO` erases and reformats a partition (os88api.inc:3588) | "WRITES ARE NEVER REDIRECTED. DSV_PKGCALL reaches three verbs, none naming a sector: ARM, DISARM, and HDV_TOOL, whose HDI_GO can ERASE a partition through the install engine - so the door's safety is the engine's refusal (hd_iact_ok), not the verb list (SPEC.md 117.4)." |
| 15 | drivers/hdd/hdd.asm:1569-1572 | "THE DOOR, and it reaches two verbs, neither a sector: HDV_ARM and HDV_DISARM (52.1.4)" | as 14 | "THE DOOR: HDV_ARM, HDV_DISARM (52.1.4) and HDV_TOOL (117.4). None takes an LBA; HDV_TOOL's install verbs write whole partitions through the engine" |
| 16 | apps/os88api.inc:3490-3493 | "HDD.DRV's reaches HDV_ARM and HDV_DISARM and nothing else, so no .O88 on the floppy can name DRVC_DISK here and arrive at a raw sector write." | as 14 | "HDD.DRV's reaches HDV_ARM, HDV_DISARM and HDV_TOOL. No verb takes an LBA, but HDV_TOOL's HDI_GO can erase a partition the install engine accepts (SPEC.md 117.4)." |
| 17 | apps/os88api.inc:3550 | "Its package door reaches TWO verbs and no sector (SPEC.md 52.1.4)" | three verbs (3564) | "Its package door reaches THREE verbs and none takes a sector (SPEC.md 52.1.4, 117.4)" |
| 18 | SPEC.md:103347-103350 (52.1.4) | "the door reaches two verbs and no sector ... the reason the door was 0 ... still holds." | as 14 | append: "(§117.4 added a third, `HDV_TOOL`. It names no sector either, but it reaches the install engine's erase, so since then the fence that matters is `hd_iact_ok`'s.)" |
| 19 | drivers/hdd/hdcom.inc:117 | "CF = 1 = past the drive" | refuses cylinder ≥ 1,024 (:142), not ≥ HDD_CYL | either hdk-6's 0-byte fix, or "CF = 1 = past what CHS can name (cylinder 1,024 and up) - NOT past the drive's own last cylinder" |
| 20 | drivers/hdd/hdd.asm:510-517 | "Every write in this driver is one sector, so without this a write fault is reported to the kernel as success" | the kernel hands multi-sector writes to DSV_BLK: data runs (`dskw_flushrun`, diskw.inc:1806-1825) and FAT ranges (4955-4979). `hd_xfer_ide` issues them as one command | "The last sector of every write - and a write is a whole run, data or FAT range - has never had its status looked at; without this a write fault on it is reported to the kernel as success" |
| 21 | drivers/hdd/mount.inc:20, :112 | "[hd_msg] says what happened" / "[hd_msg] set on a failure" | the byte is `[hd_msgc]`, an HDM_* code (hdcom.inc:45) | "[hd_msgc] (an HDM_*) says what happened" / "[hd_msgc] set on a refusal" |
| 22 | drivers/hdd/mount.inc:110-113 | "out: [hd_nmnt] incremented on success ... clobbers: flags" | `hd_svc_mount` reads CF as "hd_mount_one's verdict" (hdtool.inc:619-620); on `.badvol` CF is `OSAPI_VOL_DEL`'s | as hdk-8 |
| 23 | drivers/hdd/hdcom.inc:5; mount.inc:396, :452 | "(SPEC.md 52.12)" | 52.12 is "Every label on both surfaces is one opaque run"; the split's shared-code rule is 52.11/52.11.1 | "(SPEC.md 52.11.1)" |
| 24 | drivers/hdd/hddabi.inc:49-50 | "It matches the kernel's own spare rows (DVOL_MAX - 2)" | `DVOL_MAX` is 8 on kern_big (disk.inc:227), so DVOL_MAX - 2 = 6 | "It is the four primaries a table holds; the kernel's DVOL_MAX (8) leaves room for them beside four floppies (disk.inc:227)" |
| 25 | drivers/hdd/hdsvc.inc:312-313 | "or a resident too old to know the verb, which refuses it and leaves the 0FFh standing" | `hd_tool_need` refuses any tool whose `HD_ABI_VER` differs (hdtool.inc:179-180), so an older resident never runs this tool | "(0FFh = none; a resident of another vintage never gets here - HD_ABI_VER refuses the pair at load)" |
| 26 | SPEC.md:106705 (52.13.6) | "image + bss **5,120 → 3,584**" | the image is 4,096 today (§117.4 itself says "still 4,096 bytes and a 4KB claim") | append: "(4,096 since §117.4's door and the XMS read moved into what was the pad; the claim is still 4KB)" |

## Open questions

1. **Two geometries on one disk.** `diskread.md` dr-1: the kernel addresses
   an un-taken BIOS boot partition with the BPB's geometry after the first
   read. HDD.DRV addresses the same drive's OTHER partitions with AH=08h's,
   or a typed one. On an FN33-style disk (a 17/4 volume on a 63/16 ROM) the
   two maps place equal LBAs at different physical sectors. Can C: (the
   kernel's) and D: (the driver's) then overlap physically? An answer needs
   FN33's own image and `os88fat.py`, not a boot.
2. **Where is the Video Player's arm buffer?** hdk-1's cheap fence (DX ≥ the
   caller's ES) is only right if the chunk is at or above the package's own
   segment. A claim placed below its region would be refused. Read
   `vp_htg`'s owner before choosing the fence.
3. **Can a heap compaction run between ARM and DISARM?** Another task's
   claim during the read could trigger one. If the armed buffer is in a
   movable region, `hd_xa_seg` goes stale and the move's new tenant inherits
   the redirect. `[sch_lock]` is up across each DSV_BLK, but not across the
   whole `READ_SEQ`.
4. **hdk-3's 11h on a multi-sector command.** Does the IBM XT fixed-disk
   adapter (and the ST-11M) stop at the corrected sector, or finish the
   command? That decides between "accept" and "re-run one at a time". It
   needs the 5150 (docs/FIELD-MACHINES.md), or a WD1002 datasheet.

## Byte budget for a "now" batch (resident HDD.DRV)

| item | bytes |
|---|---|
| hdk-8 drop the redundant `jc` | -2 |
| hdk-9 DSV_CPNAME → header +16 | -11 |
| hdk-10 OSAPI_VOL_SYS | -4 |
| hdk-6 `cmp ax,[di+HDD_CYL]` | 0 |
| hdk-12 `rep outsw` | +2 |
| hdk-1 DX fence | +6 |
| hdk-13 bounce cap | +4 |
| hdk-3 11h, single-sector re-run | +12 |
| hdk-4 rung-1 retry | +10 |
| hdk-5(1) status bytes (hdcom, both images) | +14 |
| hdk-5(2)/hdk-7 base duplicate guard, `hd_mount`'s own test removed | +10 |
| **net** | **+41** (with hdk-11's ~-100: **~-59**) |

Without hdk-11 the batch crosses the 4,096 rung, and the claim becomes
4,608. The batch is correct on its own; this line only says what it costs.
With hdk-11 it gives bytes back.
