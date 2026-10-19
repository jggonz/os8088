# `.cold` in the HMA - and what the rest of it holds

> **WAVES 1 AND 2 ARE BUILT - SPEC.md 2.10.7 and 2.10.8 are the contract.**
> On a 286 or better with memory above 1MB, kern_big runs its `.cold` from the
> HMA and the heap gets the whole cold rung back: **38.0KB**, measured under
> QEMU (`tests/hmacold.py`), the same rung the 5150's ROM gives
> (docs/plans/ROM-PLAN.md). It is the ROM's mechanism with the ROM taken out:
> the same adapter (`boot/osrom.asm`, kind 3), the same lists, the same
> refusals, loaded from a file instead of found in a socket. **Wave 2 puts
> what `.cold` does not fill to work** (section 5): SOUND.DRV, HDD.DRV and
> NET.DRV load into a pool UNDER `.cold`, and SOUND.DRV's 8KB double buffer
> goes there too - **16.0KB of heap claims at boot, 24.0KB while a stream
> plays**, measured by `tests/hmadrv.py`.
>
> * **Resident cost: +2 bytes on kern_big for wave 1** (a `.bss` word became
>   an initialised `.text` word, below), **+242 for wave 2** (`.text` +38,
>   `.cold` +204, against the commit before it; `NO_HMA_DRV` is the A/B for
>   all but the API cell and its stub), **+8 on kern_small** (the cell, and a
>   stub that refuses). On a machine with the HMA, `.cold` is not in
>   conventional memory at all, so what wave 2 costs there is its 38 bytes of
>   `.text`.
> * **XMEM.DRV's claim goes 1KB -> 2KB** on the machines that load it (a 286
>   or better that reported memory above 1MB): its image went 1,014 -> 1,145
>   bytes, the loader and the A20 re-open below. Its scratch claim for
>   COLDHMA.SYS is 4KB for the length of the attach.
> * **COLDHMA.SYS is 2,919 bytes** on every kern_big system disk, in the root
>   beside BOOTHD.SYS and locked like it; **2,055 of them stay in the HMA**
>   (the module door, the pool and the relocation tables) and the installer's
>   half is dropped once it has run.
> * **The HMA holds, top down: the tail's resident half, `.cold` (38,651) from
>   `HMA_COLD_AT` = 6000, and the pool under it - 24,560 bytes.** Section 5 is
>   what goes in the pool and why it is under `.cold` and not over it.

## 1. The idea, and why it is the ROM's

The ask was *"toss `.cold` in it on boot, exactly what we do for ROMs, just
with HMA instead"*. That is literally the shape that shipped:

| | ROM (SPEC.md 2.10) | HMA (this) |
|---|---|---|
| where `.cold` runs | F400, U28-U32 | FFFF, linear 0x100000 |
| who finds it | the boot overlay's doorbell, in kmain_o | XMEM.DRV's attach, which has just verified A20 and claimed the HMA |
| where the adapter lives | the ROM's tail | COLDHMA.SYS, copied up behind `.cold` |
| when | before a byte of `.cold` has run | after the mount, near the end of kmain_o |
| the refusals | `.cold` byte for byte, the `.text` hash, every listed word | the `.text` hash, every listed word, no module held, the copy read back |
| heap | `mem_floor_ax`'s ROM arm, unchanged | the same arm: `[api_coldseg]` is not `COLD_HOME` |
| modules | `[rom_mfp]`'s door: SIZE / COPY / FIX | the same door, FIX only |
| hibernate | the pad byte says where `.cold` ran | the same byte, now its segment's high byte: RAM / F4 / FF |

The heap needed **no new code**: `mem_unblob_x` already recomputes the floor
through `mem_floor_ax` at the end of the boot, and the ROM arm there asks
`[api_coldseg]` - so an install that happens any time before that line is
paid out by the code that was already there.

## 2. What was not the ROM's, and cost the most to find

**`.cold`'s offsets had to start at 0x10.** The only segment that reaches
above 1MB is FFFF, and FFFF:0000-000F is the BIOS's own ROM (the reset
vector). So `section .cold` now has `vstart=0x10` and `COLD_SEG` is
`COLD_RAM - 1` - the same bytes at the same address, every offset sixteen
higher, **no encoding changed size**. The ROM moved from F401 to F400 with it
(its header is now the sixteen bytes under `.cold`'s first offset), which is
tidier than before. (Wave 2 then moved kern_big's to 0x6000 - section
5.2 - by the same mechanism, which is what this one bought.) Tools that read
`.cold` out of RAM add `COLD_VSTART`
(`tools/os88layout.py`'s `cold_span`, `tests/dispcold.py`,
`tests/dispreboot.py`); everything that resolves a symbol to a linear address
was already right, because nasm's symbol values carry the `vstart`.

`COLD_HOME` is the same number under a name `tools/os88romfix.py` never
moves, for the six compares that ask *"is `.cold` still where it was built?"*
- written as `COLD_SEG` they would make `.cold` name its own segment.

**One thing in the tree assumed `.cold`'s first byte was offset 0, and it
rebooted the machine.** `[gfx_bpfn]` was a `.bss` word whose zero meant
`gfx_blit1_x`, asserted to be `.cold`'s first byte. With the offsets at 0x10,
`call word [gfx_bpfn]` went to CS:0000: in RAM that is the last paragraph of
`.lowbss`, which happened to be zeros (`add [bx+si], al`, sliding into the
routine - silent corruption), and from the HMA it is FFFF:0000, **the reset
vector**: the first desktop paint rebooted. It is an initialised `.text` word
now, `dw gfx_blit1_x`: two bytes moved, two spent in the compare and the
restore that name the routine instead of zero.

**The install is LATE, so the ROM's lists are not the whole of it.** The ROM
adopts before `.cold` runs; XMEM.DRV attaches after the mount. Two things
that the ROM never meets:

* the **window half** (`.ovl`) is FAT data by then, so its four sites are
  NOT written - the kind-3 tail skips that list;
* **`mod_fp`'s slots** were filled by `mod_init_x` with `.cold`'s CS at run
  time (`COLDSEG_TO`, the one run-time store of it), so every unloaded entry
  pointed at `mod_gone` in the OLD rung - the first module asked for after
  the heap reused it ran the heap. The tail re-points every slot that names
  the old segment. A module HELD at install time would need its image fixed
  too, and none is in any boot measured, so the install refuses if one is.

The return path runs through the old copy for a while (drv_call, in `.cold`,
called XMEM.DRV's attach) and that is fine: the old rung is not heap until
`mem_unblob_x`, and by then nothing is on the stack in it.

**A20 must stay open for the rest of the session**, and one BIOS service is
known to close it: the IBM AT's int 15h AH=87h, which is XMEM.DRV's tier-1
transport on a 286. `xm_bios` now re-opens the gate (probe, port 92h, the
keyboard controller - the attach's own three steps, made resident) inside
the same `cli` window as the call, so no interrupt can reach `.cold` between
the BIOS's return and the gate. On a BIOS that keeps the gate as it found it
- QEMU's, and most later ones - it is one probe. **This path is not exercised
by any row**: QEMU's CPUs are 386-class and take tier 2 (unreal mode, no
AH=87h). The 286 machines in `vm/` are where it can be looked at.

## 3. What is not covered

* **A DOS program that closes A20 itself** (DOS.O88, SPEC.md 96) would strand
  `.cold`. A program that calls int 15h AH=87h directly, or writes port 92h
  or the keyboard controller, is the case. Nothing in the box hooks that
  today; it is the same class of hazard as a DOS program writing to kernel
  memory, and the box's own answer to it would be an int 15h hook there.
* **Tier 1 on real iron** - section 2's last paragraph.
* **Hibernate on an HMA machine** is wired by the pad byte: an image
  hibernated with `.cold` in the HMA resumes only onto a boot that put the
  same build's `.cold` there, which XMEM.DRV does before the Resume question
  is ever asked. `hmahiber` drives it under QEMU (5.4's last point); a real
  286's resume is not driven by anything, section 2's gap.

## 4. The other 25KB - can it buy more heap?

> **Answered by section 5, which took the third lever below** - driver
> images and a driver's DMA buffer - for the three drivers a 286 most often
> loads. The other two still stand as written.

After this, a 640KB kern_big desktop's conventional memory is **60.5KB of
kernel** and the arena; and the arena on a bare desktop holds ONE claim, a
32KB purgeable cache (measured, `tools/heapmap.py`). So there is no
long-lived kernel claim to move - more heap has to come out of the 60.5KB:

| what | KB | can it go up? |
|---|---|---|
| `.text` | 43.6 | **partly, and it is the one real lever** - see below |
| `.bss` | 5.2 | no: DS-addressed data, ISRs read it with A20 in any state |
| `.lowbss` | 5.6 | no: SS - the task stacks, `mem_tab`, the mount buffers |
| the FAT window | 4.5 | no: an int 13h target, and DMA does not reach above 1MB |
| `.vgabuf` | 0.5 | no: the planar decoder's, and too small to matter |

**`.text` to the HMA** is the only permanent lever, and it is a size pass in
shape: code moves out of `.text` into a second HMA-only section (not into
`.cold`, whose ROM window has 1,018 bytes left on kern_big), assembled at its
HMA offset after `.cold` and RAM-resident on every other machine in a rung of
its own. On a 286 every byte moved is a byte of heap; on the 8088 it is the
same byte in a different rung, plus the far-call bytes the move costs. What
can move is what is not on an interrupt path and not hot on the 8088 - not
the graphics and text primitives, not the scheduler, not the API cells - and
an honest yield is not known until somebody walks `.text` with that question.
**A guess of 5-10KB is a guess.** Building it is a decision about the 8088's
far-call cost as much as about the 286's heap, so it is not done here.

**Modules executed in place** are the second lever, and it is transient: the
seven kern_big modules are 34KB of images (CTRL.DRV 12.3K and DISKOPS.DRV
10.3K the largest), and while one is open it holds a claim of its image plus
its `.bss`. Assembled at fixed HMA offsets, with `mod_need` running a RAM copy
at `claim - vstart/16` the way XMEM.DRV calls COLDHMA.SYS, the ones that fit
the 25.7KB would hold no heap at all on a 286 and would not be read twice.
What it buys is heap *while the Control Panel is open*, which a 600KB machine
rarely misses.

**Driver data** is the third, and per driver: ETHER.DRV's 14KB socket pool is
data, and data can live at FFFF:x - but only through code that takes a base
offset, because everything at segment FFFF shares it and `.cold` already has
offset 0x10. That is a change to the driver for the machines that have a NIC
and a 286.

None of the three is free the way the rung was. The rung was free because the
ROM had already paid for the mechanism.

## 5. Wave 2: drivers and their buffers in the pool under `.cold`

The ask: *"how hard would it be to load drivers into HMA? ... sound and hdd
would fit, plus one other small one ... We could use it for some claim by a
driver too"* - then *"build it out. Do sound's claim too."* Built, and the
answer to "how hard" turned out to be decided by four facts that were not
visible from the question.

### 5.1 Nothing above 1MB has an offset 0

FFFF:x is linear 0xFFFF0 + x, so a block in the HMA can never be run as
SEG:0000 the way every heap claim is. Every driver in the tree is linked at
org 0, its dispatcher at offset 12 (`PKG_DISP`), and the kernel builds a far
call to SEG:000C in six places. So an HMA driver runs from a copy linked at
`DRV_HMA_ORG` = 0xE000 - one origin for every block - and its segment is
C = FFFF - (0xE000 - b)/16 for a block at HMA offset b. The six places test
the segment: at or above `DRV_HMA_SEG` (F000) the dispatcher is E00C. That
is `drv_pkg_disp`, the class pairs `drv_publish` writes, `drv_blk_call`'s
staged pointer, a window's `W_DISP`, a relocation proc's far pointer, and
the two header reads. A block is at most 8KB (E000-FFFF), which is SOUND.DRV
and the double buffer exactly.

### 5.2 The pool has to be UNDER `.cold`, and that moved `.cold`

With `.cold` copied to FFFF:0010 (wave 1), what was left was the HMA's top
25KB - and every C there is FBxx-FFxx, which is **the heap's owner tags**
(`MEM_PG_*` FB-FE, the `MEM_K_*` FFxx). A claim an HMA driver made - SOUND.DRV's
staging pool is one - would have been owned by "a purgeable level". So
kern_big's `.cold` now starts at `HMA_COLD_AT` = 0x6000 everywhere (RAM, ROM
and HMA: the section's `vstart`, as wave 1's 0x10 was), the pool is
FFFF:0010-5FFF, and every C is F200-F7FF. No `.cold` encoding depends on it
(measured: the same file size at 0x10 and 0x5E00); the ROM's segment becomes
F400 - (vstart - 0x10)/16 = EE01; kern_small keeps 0x10.

### 5.3 Relocation, not a second file

A second copy of each driver on the disk would be ~13KB of a 360KB system
disk with 29 clusters left. Instead the build assembles each HMA driver
twice (`$(HMADRV)` = sound hdd net, org 0 and org E000) and
`tools/os88rom.py hma` diffs the pair: with E000's low byte zero, **every
relocation is one byte gaining E0h**, so the table is a list of byte
positions - delta-coded, 1,006 bytes for the three and 30 of records - and the tool proves it
by relocating the LOADED image (bss zeroed, os88drv.py's header) and
comparing it with nasm's copy. It rides COLDHMA.SYS keyed by a hash of the
org-0 image, so a driver the tail has no table for, or one rebuilt since,
is simply refused and loads into the heap as always.

`drv_hma_try` is the last step of `drv_load`, after `drv_check` and
`drv_bss` and before a byte of the driver has run: ROMF_HPLACE hashes,
allocates, copies, **reads back**, relocates in the HMA and answers C; the
row then names C, holds no KB, and the claim is freed. A failure anywhere
leaves the claim exactly as loaded.

### 5.4 What the HMA copy broke, and what fixed it

Found by `tests/hmadrv.py`, each as a red row first:

* **`mem_own` refused every claim an HMA driver made.** It recognises a
  caller by a heap claim STARTING at its segment, and an HMA driver runs in
  none. HDD.DRV's twin proof and its own buffers were refused, the IDE disk
  fell back to the BIOS rung, and nothing mounted. It asks `drv_owns_seg`
  for a segment at or above F000 now.
* **The installer's half did not fit.** COLDHMA.SYS was 2,920 bytes and,
  over `.cold`, 88 past FFFF. The installer and its lists (the `.text` hash
  spans and every far-reference list) are read once; the tail is now laid
  out installer-first and only `rom_res` onward - the module door, the pool,
  the tables - is copied up: 2,055 bytes.
* **The BIOS cannot DMA above 1MB** - the IBM AT's diskette code keeps four
  page bits. `dsk_runcap` answers 0 for an HMA buffer, so the kernel bounces
  it a sector at a time through `dsk_secbuf`; HDD.DRV's `hd_bios_run`
  refuses one and its `hd_raw` bounces its own partition table through a
  1KB heap claim. HDD's code then crossed its own `align 512` by 45 bytes,
  so HDD.DRV is 4,096 bytes where it was 3,584 - its heap claim was 4KB
  either way.
* **An interrupt into the HMA while the BIOS has A20 shut** - found on a
  286 in the field, after the wave shipped: a video buffering into XMS
  locked the machine within seconds and played fine on the build before.
  Section 2's re-open ran after AH=87h returned, under the caller's `cli` -
  but the IBM AT's int 15h `sti`s on entry and shuts the gate before its
  return, so an IRQ0 in that tail reached `snd_tick` and SOUND.DRV's tick
  in the HMA with the gate shut. Wave 1 never met it because nothing an
  interrupt runs was in `.cold`; wave 2 put SOUND.DRV's ISR and tick up
  there. `xm_bios` masks the master PIC across the call and restores it
  after `xm_a20_reopen`. `vidxmsbios` runs that path under QEMU with a test
  build that forces the BIOS transport, and goes red (the clock stops) if
  the mask is not restored; it cannot reproduce the window itself, SeaBIOS
  putting A20 back as it found it.
* **A driver's SECOND image, against a driver in the HMA** - found on the
  same 286 the next day: Control Panel -> Hard Disk froze the machine with
  the file progress bar at 100%. Two defects, each invisible to every
  installer row because those run on MartyPC's 8088, where nothing is in the
  HMA. HDDTOOL.DRV called back into HDD.DRV at PKG_DISP:CS where the
  dispatcher is at E00Ch (HDT_INIT now passes it, HD_ABI_VER 6); and
  `mem_own` recognised the tool's claims by a MEM_K_DRV claim at its owner's
  base, which an HMA driver does not have, so Install refused with *Not
  enough memory to copy* (+11 bytes of `.cold`). `hmadrv` now opens the page
  and runs the whole install with HDD.DRV in the pool.
* **Hibernate carries the pool** (SPEC.md 87.4 step 3b, 87.6 step 0b). The
  first cut swept every HMA driver whatever its class, because nothing above
  1MB is in the image - and on a floppy-booted 286 whose only C: is HDD.DRV's
  that took down the volume the image was about to be written to, so the
  hibernate refused. Now an HMA driver follows its class like a heap one,
  and after the sweep the module asks the tail where its block table is
  (ROMF_HTAB) and copies the table and every live block into a `MEM_K_HIB`
  claim, which the image carries because it is conventional memory. The
  wake copies them back to the same bases before the `sti`; with no copy it
  empties the pool (ROMF_HRESET) as before. 0 resident bytes, +270 of
  HIBER.DRV inside its 7KB claim, 18 of the tail, no disk I/O.
  `OSAPI_DRV_SUSPEND` keeps an HMA driver standing.

### 5.5 SOUND.DRV's claim

`OSAPI_MEM_HMA` (0x0487) is a driver's buffer in the pool: AX = bytes ->
DX:AX, AX never 0. Only a loaded driver may ask, and `drv_release` gives
back whatever a driver still holds. `sbl_dma_get` asks it first for the 8KB
double buffer and record ring and falls back to the heap; the CPU side
addresses it from `[sbl_sbase]` and the 8237 sees page 10h - the AT's page
register is eight bits and the whole HMA is one 64KB page. The staging pool
stays in the heap: it is sized to the ask, grows, and moves.

`tests/hmadrv.py` plays RECORDER's demo out of it under QEMU's SB16: the
block is in the pool owned by SOUND.DRV, both halves hold the demo's
samples in stream order, the card plays to the end, and the block goes back
with the stream.

### 5.6 What it buys, and what is not covered

| at boot (SOUND + HDD + NET wanted) | heap claims |
|---|---|
| `-m 1` (no HMA) | SOUND 8KB, HDD 4KB, NET 5KB = 17KB |
| `-m 128` | none - the three are 7,264 + 4,096 + 4,160 bytes of the pool |

...plus 8KB more while a stream that is not direct plays. The pool is
24,560 bytes; the three images and the buffer are 23,712, so all four fit
with 848 to spare, and **a `.cold` that grows takes that spare first** -
past it, `tools/os88rom.py` refuses the build and `HMA_COLD_AT` comes down,
which is a driver that no longer fits rather than a machine that breaks.

Not driven by any row: unloading and reloading an HMA driver from the
Drivers page (`drv_release`'s ROMF_HDROP, then a fresh placement); and tier
1 on real iron
(section 2). **A20 matters more now**: SOUND.DRV's ISR and `snd_tick`'s call
into it run from the HMA, so a DOS program that closes A20 (section 3) stops
the sound driver as well as `.cold` - and the BIOS's own AH=87h did exactly
that until `xm_bios` held the PIC across it (5.4).
