# Mounting a `.IMG` as a drive

**Status: BUILT - SPEC.md 18.101 is the contract and this file is the design
record behind it.** It was written as an investigation against a prototype
that was measured and run under MartyPC; the owner then asked for it with
read-only, several images at once and kern_small kept out ENTIRELY, and it
shipped at **198 resident bytes** where the prototype was 164. §9 below is
what the build changed against this plan, and §3-§6 are kept as written,
because the reasoning in them is why the built thing has its shape. Figures
marked MEASURED come from builds compared at one commit (SPEC.md 14.2);
ESTIMATE figures were not built.

The brief was *"investigate the ability to mount a `.img` as a drive. This
probably shouldn't be resident, unless it's 200 bytes or less"*, plus two
follow-up questions from the owner, which §7 and §8 answer:

- what it would cost and gain to route everything an end user does through the
  redirectors, and whether that makes sense;
- what it would cost to support more than one redirector.

## 1. The answer

**It fits under 200 resident bytes, and it is a SECTOR-layer feature, not a
file-layer one.** A mounted image is an ordinary volume row whose sectors are
somebody else's sectors. The kernel's whole FAT layer then runs on it
unchanged: mount, listing, icons, copy, delete, New Folder, the file dialog
and the package loader. Only `dsk_xfer`, where a sector is finally addressed,
has to know.

| | resident bytes | status |
|---|---|---|
| `dsk_xfer`'s image arm, read-only flag included | **+164** (`.cold` +162, `.text` +2) | MEASURED |
| ...without the read-only flag | **+142** (`.cold` +140, `.text` +2) | MEASURED |
| the volume row itself | 0 - `dsk_vol_add` already stamps a `DVK_DRV` row of any class (§4) | read off the source |
| the UI: pick a file, validate, walk extents, add the row, unmount | 0 - an on-demand module (§5) | ESTIMATE |
| a second simultaneous image, and each one after | +~16, then +2 each | ESTIMATE |

The +164 spends the 71 bytes left in kern_big's cold rung and crosses it
(`KERN_SIZE` 97,792 -> 98,304, spare 3,072 -> 2,560). Per CLAUDE.md the rung is
not the point, but say so when this lands: the next 348 bytes of `.cold` are
already paid for.

**Run on the machine.** A 5150 with two 720KB drives on GLaBIOS
(`os8088_5150_herc_720_gla`). B: carried a 720KB volume holding `TEST.IMG`, a
360KB os8088 floppy with one file on it. The row and an extent table were
written in before the desktop's first layout (§6). Then:

- **D: mounted and listed `HELLO.TXT`.** The table was deliberately SPLIT into
  three runs at image sectors 0, 3 and 7, where the file is really
  contiguous, so the FAT read (sectors 1-4) and the root directory read (5-11)
  each crossed a run boundary and went out as two host transfers.
- **New Folder on D: created `GATE`.** Read back on the host, exactly four
  host sectors changed: 15, 17, 19 and 28. Those are the image's FAT1, its
  FAT2, its root directory and `GATE`'s cluster, each mapped through a
  different run. Nothing outside `TEST.IMG` moved, and both the host volume
  and the image pass `os88disk.py --verify`.
- **With the read-only flag set, New Folder `RONLY` was refused** and nothing
  was written. Whether the refusal was *announced* is not established: the
  screenshot taken after it shows no message, and §10 lists it (answered: `Write protected`).

## 2. Why the sector layer, and not a redirector

There are three places an image could be served from.

**A redirector (`DRVC_FILE`, SPEC.md 62.9) is the wrong one, for two reasons
that each rule it out alone.**

1. **It would have to re-implement FAT.** A redirector answers questions about
   FILES: `FSV_LIST`, `FSV_STAT`, `FSV_READAT`, `FSV_WRITE` and the rest. An
   image's files are inside a FAT volume, so the driver would have to parse
   and write that FAT itself, several KB of driver duplicating code the kernel
   already has resident.
2. **It cannot read its own backing file.** The FSV verbs are called from
   INSIDE the kernel's file layer, on a volume that is current. Reading the
   host file would mean `OSAPI_FILE_READ_AT` on another volume from inside
   that call. `disk_mount` keeps no cross-mount state (SPEC.md 18.3), and
   `[dsk_cwd]` and the listing snapshot are machine-global. So the inner
   call remounts the host and destroys the outer operation's state. The RAM
   disk never meets this because its store is memory. Fixing it means making
   the file layer re-entrant, which is a bigger project than this feature.

It would also take the one `DRVC_FILE` publication slot, so it could not
coexist with the RAM disk or the parallel link (§8).

**A block driver (`DRVC_DISK`'s shape, SPEC.md 18.7) is closer, and is what
the prototype looks like from outside.** The row is `DVK_DRV`, so everything
that already treats a driver-backed volume as fixed, uncached and
not-a-floppy does so here. But a real driver would have to reach the host's
sectors. The host is a BIOS floppy, the BIOS boot partition, or `HDD.DRV`'s
volume, and a driver can only reach the BIOS one by duplicating `dsk_xfer`'s
retry ladder, DMA-page bounce and EOT handling. It would also take the
`DRVC_DISK` slot from `HDD.DRV`.

**`dsk_xfer` itself is the right place because it already re-enters.** The
§18.95 cache fill calls `dsk_xfer` from inside `dsk_xfer` to read its chunk
(SPEC.md 18.95, *"The fill re-enters `dsk_xfer`"*). `DISKOPS.DRV`'s
`Write Img...` already banks and swaps `[disk_drive]` and the geometry words
to alternate between a file volume and a raw one (SPEC.md 18.99.8,
`clo_fbank`). The image arm is those two existing patterns put together, and
re-entry is what makes it short: each piece goes back through the one
transfer routine, which already does the host's CHS, its partition base, its
bounce and its retries.

## 3. The resident half, as prototyped

The row is `DVK_DRV` with `DV_CLASS` = `DRVC_IMG` (`0x41`). That is a
**pseudo-class**: like `DRVC_OVL` (`0x40`) it is outside `1..DRVC_MAX`, so it
is never published, `drv_cls_idx` refuses it by range, and
`dsk_vol_drop_drv`'s class match can never sweep it up with a real driver's
volumes. `.drv` tests it first:

```nasm
.drv:                           ; DI = LBA, SI = count, ES:BX = the buffer.
    cmp ch, DRVC_IMG            ; CH = the row's DV_CLASS, already loaded
    je .img
    ...
```

The claim `[dsk_imgseg]` names starts with the HOST's `disk_drive`,
`disk_spt` and `disk_heads`, in that order. Those three are adjacent in
`.text` (disk.inc's `disk_drive: db` / `disk_spt: dw` / `disk_heads: dw`), so
one 5-byte swap trades the whole transport in and back out:

```
0   db  host volume index      } the same five bytes, in the same order,
1   dw  host sectors per track } as disk_drive / disk_spt / disk_heads
3   dw  host heads             }
5   db  flags: bit 0 = read-only
6   dw  offset of the sentinel run
8   runs: { dw first image sector, dd host-relative LBA } ...
    sentinel: dw the image's sector count
```

```nasm
.img:                           ; DI = image LBA, SI = count, ES:BX = buffer
    test si, si
    jz .ijs
    push es
    mov es, [dsk_imgseg]
    mov bp, IMG_TAB
.ifind:
    cmp di, [es:bp+IMG_REC]     ; below the NEXT run's first sector?
    jb .ihit
    add bp, IMG_REC
    cmp bp, [es:IMG_END]
    jb .ifind
.ibad:
    pop es
.ijf:
    jmp .fail
.ihit:
    cmp byte [dsk_op], 0x03
    jne .ird
    test byte [es:IMG_FLG], IMGF_RO
    jz .ird
    mov byte [dsk_ioerr], 0x03  ; write-protected, int 13h's own word for it
    jmp .ibad
.ird:
    mov cx, [es:bp+IMG_REC]
    sub cx, di                  ; sectors left in this run
    cmp cx, si
    jbe .icnt
    mov cx, si
.icnt:
    mov ax, di
    sub ax, [es:bp]
    xor dx, dx
    add ax, [es:bp+2]
    adc dx, [es:bp+4]           ; DX:AX = the host-relative LBA
    mov [dsk_lbahi], dx
    pop es
    call .iswap                 ; the HOST's volume and geometry in
    cmp byte [dsk_op], 0x03
    jne .ixf
    call dsk_rah_wr             ; the HOST's cache, keyed by the host volume
.ixf:
    call dsk_xfer               ; re-entered, as the cache fill already does
    call .iswap                 ; ...and the image's back (CF survives)
    jc .ijf
    add di, cx
    sub si, cx
    shl cl, 1
    mov ch, cl
    xor cl, cl                  ; CX = count * 512 (count < 128: one buffer)
    add bx, cx
    jmp .img
.ijs:
    jmp .success
.iswap:                         ; no flag is written: mov, xchg, push, pop,
    push es                     ; dec and jns leave CF alone
    push ax
    push bp
    mov es, [dsk_imgseg]
    mov bp, 4
.isw:
    mov al, [es:bp]
    xchg al, [ds:bp+disk_drive]
    mov [es:bp], al
    dec bp
    jns .isw
    pop bp
    pop ax
    pop es
    ret
```

It sits after `dsk_xfer`'s `.out`, inside `%ifdef OS88_DRIVERS`. Placed beside
`.drv` it pushed `.sector` out of short range for two existing jumps.

**Why the nested call is safe at this point.** By the time `.drv` is reached
the outer call has taken `[sch_lock]` and pinned the destination (`push word
[mem_pinseg]`, which is documented as NESTED and not owned). It has stored
`[dsk_unit]` and `[dsk_op]` and will read nothing else. Its only exits are
`.success`, which reads `[dsk_bnc_si]` (the inner call leaves it 0), and
`.fail`. The inner call's `.out` zeroes `[dsk_lbahi]`, which the outer one
never needs: an image is under 32MB. `[dsk_op]` is the same on both levels.

**What it gets right for free:**

- **Every host-specific fact comes from the host's row.** `dsk_geom_pick`
  reads `dsk_vbase[host]` (a boot partition's base, SPEC.md 52.10.3) and the
  cylinder bound off the host unit. A DVK_DRV host goes out through
  `HDD.DRV`'s `DSV_BLK` with a host-relative LBA, which that driver adds its
  own base to. CHS uses the host's geometry because the swap put it there,
  and that is the fact that rules out the cheaper "an image is a partition"
  shape (§4).
- **The §18.95 cache stays coherent.** Writes call `dsk_rah_wr` with
  `[disk_drive]` = the host and host-relative LBAs. That drops exactly the
  host slots a host-side read could have filled over the image's file data.
  Image reads skip the cache, because `[dsk_vkind]` is the image mount's
  `DVK_DRV`, so nothing image-filled can ever be served stale.
- **The DMA page, the bounce, EOT, the head-crossing run and the retries are
  all the host transfer's own.** Each piece is an ordinary `dsk_xfer`.

## 4. Two cheaper-looking shapes, and why they lose

**"An image is a partition"** would be a `DVK_BIOS` row with a
`dsk_vbase` = the file's first host sector, at roughly 20 bytes. It works only
for a CONTIGUOUS file, and it fails silently on geometry. `disk_mount`
replaces `[disk_spt]`/`[disk_heads]` with the BPB's ("all 16 rules passed:
the geometry fields become disk_read's own"), and an image's BPB describes the
360KB floppy it was taken from, not the 1.44MB floppy or 17-sector hard disk
it now sits on. So every CHS would land somewhere else, and the first read
fails the same way SPEC.md 52.10.3 already records for the boot partition
under a 9/2 geometry. Fixing that needs a per-row transport geometry, which is
the swap above, and fragmentation needs the run table. Once both are added it
is the prototype.

**Contiguous files only** would save the run walk, about 20 bytes, and is not
worth it. `dskw_alloc` is next-fit from `[dsk_rover]`, which resets at every
mount (SPEC.md 18.4), so an image copied onto a disk that has ever had a file
deleted from it is usually fragmented. Images `os88disk.py` builds are
contiguous, but images the user copies are not, and those are the ones they
will try.

## 5. The non-resident half (ESTIMATE, nothing built)

**Where it lives: `DISKOPS.DRV`, as a `Mount Img...` stop beside
`Write Img...`.** Every piece of the front end is already in that module: the
Open dialog, `clo_fnbuf`, the four-layout size test that answers
`CERR_NOTIMG` for anything that is not a standard floppy (SPEC.md 18.99.8),
and `CERR_SAME`'s volume-index reasoning. The Clone Disk window is already
reached from a menu, so the entry point costs no resident byte either. A
menu item of its own in the Disk window is the discoverable alternative, at
the price of a menu row and a greying predicate, which are resident.

`Mount Img...` does six things, all at the UI task with the host volume
current:

1. **Refuse an unsuitable host.** A `DVK_FILE` volume (the RAM disk, the
   cable) has no sectors at all, and an image on a mounted image would need
   the swap twice. Both refuse in words.
2. **Walk the chain into runs**, LBAs host-relative. `HIBER.DRV`'s
   `hbm_extat` is this walk already (`{dd lba, dw count}`, SPEC.md 87.5),
   but two modules cannot far-call each other, so it is lifted or
   `%include`d rather than called.
3. **Claim the table with `OSAPI_MEM_CLAIM_HI`, pinned.** It is 8 + 6n + 2
   bytes, and claimed top-down it cannot wall the arena (SPEC.md 50.3.2.1).
   Cap n (64 runs is under 400 bytes) and refuse past it with *"Too
   fragmented to mount - copy it to an empty disk first"*.
4. **Mark the file READ-ONLY on the host while it is mounted.** This is the
   one hazard that corrupts somebody else's data (§6), and the FAT layer
   already refuses read-only on delete, replace, append and rename
   (`DSKW_PROT`/`DSKW_NPROT`), so the guard costs no resident byte. There is
   no attribute setter in the kernel, so the module writes the entry's byte
   itself. A file that was ALREADY read-only mounts with `IMGF_RO` set.
5. **Bank `[disk_drive]`, `[disk_spt]` and `[disk_heads]` into the claim's
   first five bytes**, which is `clo_fbank`'s job exactly, then set
   `[dsk_vcls]` = `DRVC_IMG` and call `dsk_vol_add`. That routine takes the
   first free row from D: up, stamps `DVK_DRV`, reflows the desktop and
   claims the volume's FAT window. It needs no change.
6. **Unmount is the reverse:** free the row (`dsk_vol_del`'s driver arm
   already handles `DVK_DRV`), clear the read-only marker unless it was the
   user's, and free the claim.

`kern_small` is a separate decision. Its `dsk_xfer` has no `.drv` arm at all
(`OS88_DRIVERS` is off), so the dispatch has to come back with the image arm,
and it has one free row (D:). Build it on kern_big first.

## 6. What can go wrong

| hazard | consequence | answer |
|---|---|---|
| the host FILE is deleted, replaced or moved while mounted | writes through D: land in clusters that now belong to another file - **somebody else's data** | the read-only marker (§5 step 4), at zero resident bytes |
| the machine stops with an image mounted | the marker stays on the file | mark with read-only + SYSTEM, which no user sets; a mount that finds that pair clears it rather than mounting read-only |
| the HOST volume goes away (`HDD.DRV` unticked, a floppy drive retired) | the inner call finds no row and fails: `Disk error`, never a wrong sector | acceptable as it stands; dropping image rows with their host is one test in `dsk_vol_del`, resident, so it is the owner's call |
| a floppy host's disk is swapped | **as first built, the next floppy is WRITTEN**: the image row is `DVK_DRV`, so it banked its BPB for good, `dsk_media_ok` called it not removable and no remount ever read its boot sector again - the signature this row relied on never ran, and D:'s writes landed at the image's old host LBAs on whatever disk was in the drive, every call answering CF = 0 | SPEC.md §18.101.7: the image takes its HOST's medium (`dsk_vol_rmv`, `dsk_img_host`), so one on a floppy banks for a batch only, re-reads its boot sector at every other mount, gets the host drive's motor test and never keeps a write stream past its callback - the swapped disk's bytes at that LBA fail the BPB rules before a byte is written (the residual, a disk with another valid boot sector at exactly that LBA, is written down there). 35 resident bytes |
| the image is not a FAT volume | `disk_mount`'s sixteen rules refuse it, as for any disk | nothing to add |
| two images | `[dsk_imgseg]` is one word | make it a table indexed by `DV_UNIT`, which a `DRVC_IMG` row does not otherwise use: ~+16 bytes, then +2 per image |
| hibernate with an image mounted | the row and the table come back with RAM, so the host had better be the same disk | §87's resume already assumes its own volume is unchanged; refuse to hibernate with an image mounted if that is not good enough |

Not a hazard: **the host's FAT and directory are never written through the
image.** The run table names file-data sectors only. The prototype's host
diff (sectors 15, 17, 19 and 28, all inside `TEST.IMG`) is that claim
measured.

## 7. "Everything from an end user through the redirectors" - cost, gain, and whether it makes sense

Two readings, and neither is worth doing for this feature.

**Reading 1: the image is served by a redirector.** §2 rules it out on
re-entrancy and on duplicating FAT, and it would also take the only
`DRVC_FILE` slot.

**Reading 2: the redirector interface becomes the ONLY way in, with FAT as
one provider behind it.** Every file operation would dispatch through a
per-volume verb table, so the kernel's FAT code becomes "the FSV table of a
`DVK_BIOS`/`DVK_DRV` volume".

*What it would gain:*

- **One dispatch instead of 66 arms.** The redirected arms are 28 sites in
  `disk.inc`, 21 in `diskw.inc`, 12 in `filecp.inc`, 3 in `fdlg.inc` and 2 in
  `loader.inc`, each a `DV_KIND` test at the top of a body (SPEC.md 62.9.3).
  A table removes the tests and the bug class of an arm somebody forgot.
- **New filesystem types become drivers**, such as ISO9660 for the live CD
  (SPEC.md 80), CP/M disks for RunCPM, or a host passthrough under an
  emulator. That is the real prize, and it does not need reading 2: a second
  redirector class gives all of them (§8).

*What it would cost:*

- **The FAT code cannot leave the kernel.** The kernel loads drivers and
  modules off FAT (`mod_need` reads through `dskw_read_x`, and that cone is
  155 symbols, docs/plans/completed/KERN-SMALL-MODULE-SPLIT.md), and kern_small
  and kern_dos load no drivers at all. So FAT stays resident behind the
  table, and the change is a re-plumbing with no bytes moved out.
- **The FSV ABI is far narrower than the FAT layer.** It has no held
  read/write streams (SPEC.md 18.4.8.1, 18.4.9), no header peek for the icon
  harvest (62.9.2.2), no cluster-level copy and no multi-select set
  operations. Either the ABI grows to match the kernel's internal surface,
  which commits it as published driver ABI for ever, or the user-visible
  features shrink to what a redirector can do.
- **Bytes go the wrong way on the small machine.** kern_small carries NONE of
  the redirected arms today, having had **-331** bytes taken off by gating
  them (SPEC.md 62.9.2.3). Making the table the only path puts an indirect
  call and an adapter per verb back onto the build that can least afford
  them. On kern_big it is roughly a wash (ESTIMATE): ~66 compare-and-branch
  arms out against ~15 register-shape adapters and the table in.
- **Its regression surface is every file operation the machine has**, which
  is the whole soak.
- **Speed is no argument either way.** An indirect near call is cycles
  against a ~400 ms `int 13h`.

**Verdict: no.** It is an architecture change whose one real gain (pluggable
filesystems) the next section buys for under 100 bytes. And it makes image
mounting HARDER, because the cheapest correct image mount is below the
filesystem, where §3 puts it.

## 8. More than one redirector - what it costs

**BUILT, as SPEC.md 62.9.19, at 103 resident bytes** against the 70-90 below.
The estimate counted a 28-byte copied table and `DSV_SIZE` is 36 now; the
first build was 136, and taking `drv_pkg_call_x`'s inline compaction out for
the shared `drv_cls_svc_x` (one far call), asking `drv_fs_has` once instead of
twice and dropping an unreachable refusal arm gave back 33. The owner asked
for the trim before the commit. The table below is the estimate as written.

**Today there is exactly one, and it is hard-coded.** `drv_fs_call` and
`drv_fs_has` read `[drv_fseg5]` and `[drv_svc + (DRVC_FILE-2)*DSV_SIZE +
DSV_FS]`, which is class 5 as a constant (`kernel/driver.inc`). Publication is
one slot per class and `drv_load` refuses a second driver of a class with
`DRVE_TWICE` (SPEC.md 51.2.1), so **`RAMDISK.DRV` and `NET.DRV` are mutually
exclusive at run time today**, which that section states as reachable rather
than theoretical. `drv_fs_call`'s own header names the fix: *"the day a second
one does, this grows a `[drv_fscls]` written out of the volume row"*.

**The cheap version: a second file class (ESTIMATE ~70-90 resident bytes on
kern_big, 0 on kern_small).**

| item | bytes |
|---|---|
| `DRVC_MAX` 7 -> 8: `drv_owner` +2, the `drv_fptr`/`drv_fseg` pair +4, a `drv_svc` copy +28 (`DSV_SIZE`) | +34 `.bss` |
| `drv_cls_svc_x` refuses the no-copy classes by `>= DRVC_POINT`, which assumes they are the LAST two. A copied class 8 after them needs an explicit range | ~+6 |
| `disk_mount` banks the current volume's `DV_CLASS` as `[drv_fscls]` beside `[dsk_vkind]` | ~+6 |
| `drv_fs_call`/`drv_fs_has` index by `[drv_fscls]` through `drv_cls_fp_x`/`drv_cls_svc_x` instead of the class-5 constants | ~+25 |
| `dsk_vol_add` stamps `DVK_FILE` for either file class | ~+4 |
| `drv_tab`: move `RAMDISK.DRV`'s row to the new class | 0, but a class number is ABI (SPEC.md 51.2.1): the driver and anything calling `OSAPI_DRV_CALL` with `BH = DRVC_FILE` for the RAM disk (`RAMPAGE.DRV`, `apps/dos`) are rebuilt with it |

kern_small pays nothing: `OS88_REDIR` is off there, so no arm exists to
change. What it buys is the RAM disk and the cable at once, and a slot for the
next file-shaped driver: a CP/M filesystem, ISO9660, or the host passthrough
SPEC.md 62.9 lists first.

**The general version is N drivers per class, with the publication slot per
driver rather than per class.** It is not recommended. Every `[drv_svc+DSV_*]`
read in the kernel (the sound layer's especially) assumes one driver per
class, so it is a rewrite of the publication model for a need that one more
class number meets.

**Image mounting is independent of all of this.** `DRVC_IMG` is a pseudo-class
with no publication slot, so an image mounts beside the RAM disk, the cable
and the hard disk, whichever of them is loaded.

## 9. What the build changed

1. **The entry is a DOUBLE-CLICK, not a Clone Disk button.** §5 assumed a
   Clone Disk window with buttons; there is none - Clone Disk and Write Img
   are status-line prompts on the ordinary Disk window, driven by keys, and
   the kern_big File menu is FULL (11 of `MENU_POPMAX`'s 11). So
   `fm_open_sel` tests a data file's name for `.IMG` before the association
   table, and the second double-click is the eject. 45 resident bytes,
   inline.
2. **The module was renamed** `CLONE.DRV` -> `DISKOPS.DRV` at the owner's ask,
   in its own commit before this one (SPEC.md 18.99).
3. **Several images: the segment moved INTO THE ROW.** A per-volume table was
   +29; the last two bytes of the row's label (`IMG_DVSEG`), loaded by the
   row lookup dsk_xfer already makes, are +5.
4. **The read-only marker is read-only + byte 12 = `0xC1`**, not read-only +
   SYSTEM as §6 proposed: the listing hides SYSTEM entries (SPEC.md 19), so
   the mounted file would have vanished from the window that unmounts it.
5. **The flag test is one `test`**: `IMG_FLG` is a mask of the int 13h
   functions an image refuses, ANDed with `[dsk_op]`.
6. **`dsk_rah_wr` runs on every piece**, read or write - seven bytes cheaper,
   and dropping a slot on a read costs only the slot.
7. **The image is proved by mounting it** (`fcp_goto` on the new root) rather
   than by a size table, so any FAT volume that is a whole number of sectors
   under 32MB mounts, and anything else is `Not a disk image` with no row and
   no claim left.

The cost, from the listing: arm and swap 140, the double-click test 45,
dispatch 8, the row load 3, `[dsk_imgcur]` 2 - **198**, against the owner's
200. kern_small and kern_dos are byte-identical to the tree before.

## 10. Open questions (as written, before the build)

1. **Does the read-only refusal say so?** ANSWERED: `Write protected`,
   asserted by `tests/imgmount.py`.
2. **The cache on a floppy host.** Image reads skip §18.95, so a 360KB image
   on a floppy loses the revolution savings a native floppy gets. Swapping
   `[dsk_vkind]` in with the geometry would let the inner call cache under
   the host volume, and coherence would still hold because writes drop by
   host volume + LBA. That costs a few bytes and has not been measured.
3. **kern_small**, at about the same price plus its missing `.drv` dispatch,
   for a machine with one free row.
4. **Hard-disk images** (`os8088-usb.img`, partitioned) are out of scope: the
   run table maps one FAT volume, and a partitioned image would want the
   partition walk `HDD.DRV` already does.
5. **The gate.** The prototype's check (§11) pokes the row in; the real one
   drives `Mount Img...` through `tools/os88ui.py`, then reads, writes and
   unmounts, then checks the host image byte-for-byte outside the file, plus a
   fragmented host built by deleting a file between two copies.

## 11. How the prototype was checked

`os8088_5150_herc_720_gla`, the prototype's own `os8088-720.img` in A:, and
in B: a 720KB volume built by
`os88disk.py --size 720 TEST.IMG`, where `TEST.IMG` was itself
`os88disk.py --size 360 HELLO.TXT`. `TEST.IMG` sits at host sectors 14-733,
contiguous.

1. Break on `desk_rowcalc_x`, which runs before the desktop's first layout
   pass (`kmain`, after `drv_boot_x`).
2. Write the table to 0000:0500. That is `HS_IVT`'s ground, used only during
   a hibernate, so the heap cannot reach it. Its runs start at image sectors
   0, 3 and 7, mapped to 14, 17 and 21, with a sentinel of 720. Then point
   `[dsk_imgseg]` at it and write row 3 (`DVK_DRV`, class `0x41`, 720
   sectors, label `IMG`).
3. Run, open D: through `tests/dispcp.py`'s `open_drive`, read the listing,
   press N, type `GATE`, press Enter, and read the listing again.
4. Set the read-only flag, repeat with `RONLY`, and check it was refused.
5. Flush B:, diff it sector by sector against the original, and fsck both
   the host and the image extracted from it.
