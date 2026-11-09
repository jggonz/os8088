# Loadable drivers and the sound core

Big review, 2026-11-08, branch `the-big-review`. Read-only: nothing was
assembled, booted or soaked. Line numbers are at the branch head as checked
out for this review. "kern_big" below means every `OS88_DRIVERS` build
(kern_big, kern_emu, kern_mega), because `OS88_DRIVERS` is defined iff
`KERN_BIG` (kernel/kernel.asm:979-981). kern_small has no driver layer at all.

## Scope read

| file | lines | how |
|---|---:|---|
| kernel/driver.inc | 6,911 | every proc body; the CFG_* macros; the kern_small stub arm. Skimmed only the Floppy-page cylinder test (`cpc_fdtest`, ~6300-6530) |
| kernel/drvvol.inc | 271 | all |
| drivers/os88drv.inc | 1,059 | all |
| kernel/vxabi.inc | 174 | all |
| kernel/snd.inc | 1,322 | all |
| drivers/hdd/hdd.asm | 1,599 | entry, detach, DSV_BLK, GEOM, `hd_xfer` (rung 0), `hd_ready`, `hd_services` (~650 lines) |
| drivers/xmem/xmem.asm | 1,787 | header, ABI block, entry (~150) |
| consumers, for the ABI | - | drivers/usbmouse/usbmouse.asm entry/ready/detach; drivers/sound/sound.asm attach and `snd_release_both`; drivers/sound/sb.inc `sbl_release_inst`, `sbl_stop_stream`, `sbl_v_busy`, `sbl_pool_put`; drivers/ether/ethdrv.inc's table; drivers/ems/ems.asm's table; kernel/ctrl.inc's Drivers-page click (7105-7170); kernel/wm.inc's nest pair; kernel/xmem.inc's ABI; kernel/memory.inc `mem_rr_lone`, `mem_owned_kba`; kernel/disk.inc `.drv`/`.drvhi`; apps/install, apps/dos/dosset.inc's CLASSK callers |
| SPEC.md | - | 51.0-51.2.4, 51.1.x, 51.4, 51.7, 51.12.x; 34 intro, 34.8, 34.9; 117.3 |
| plans | - | ABI-BREAK-LIST.md (all), KERNEL-BYTE-QUEUE.md, NO-PINNING-PLAN.md K1, HEAP-UNPIN-PLAN.md's drv_wcnt rows, CODE-REVIEW-TWO-CYCLES-2026-11-01.md |

## Verdict

The driver layer is one of the most carefully built parts of the kernel. Its
core model is a header that matches a package header, a per-class
publication slot, a copied service table that IRQ0 can read with no
segment load, and a length byte that makes short tables safe. It has been
hardened one field bug at a time, and the hardening is sound. Each register
rule, the stamp order (51.2.4) and the stray-row guard came from a real
failure. Resident cost is low because the attach half and the settings
parser live in images (blob, CTRL.DRV, HIBER.DRV).

Its weak point is **lifetime**. Nothing counts who is *inside* a driver or
whose worker is whose. The kernel answers "may I free this image?" with one
global worker count (`drv_wcnt`) and no in-call count. That was safe while
`SOUND.DRV` was the only driver that spawned a task. `USBMOUSE.DRV` now keeps
a worker for its whole life, and that turns the global count into a real
defect: unloading any other driver stalls for about 5 s and then leaves that
driver detached but still published (drivers-1). Smaller lifetime and
contract issues: `OSAPI_DRV_CLASSK`'s predicate is wrong for drivers placed in
the HMA (drivers-3), and `DSV_RELINST` is called at IF=0 into code that makes
kernel calls (drivers-4). The doc debt is heavy. `snd.inc`'s banner still
describes a speaker-only kernel with no sound driver, and about 30 comments
and SPEC lines describe retired designs. The two prior-art lessons worth
taking are reference counting (GEOS, AmigaOS, MMSYSTEM) and a kernel-owned
IRQ registry (OS/2 DevHlp_SetIRQ, Amiga resources). Both are small in bytes.

## Strengths

1. **A copied service table per class, read without a segment load**
   (driver.inc `%1_publish` 4040-4126, `drv_svc` 6173). `snd_tick` inside
   IRQ0 decides whether it has work with one near compare
   (snd.inc:441-443). There is no `mov es` and no far pointer read. Copy this
   wherever an ISR consults state owned by a loadable image.
2. **The table carries its own length** (`DRV_H_DSV` at +15, `DRV_H_FSV` at
   +10). The kernel zeroes the slot, then copies exactly the driver's length
   (driver.inc:4060-4107), and cell 0 is refused by every consumer's
   `or bp,bp`. This works like Win32's `cbSize`: the table can grow in both
   directions without a `DRV_VER` bump. It is better than the "all or
   DSV_TAB0" rule it replaced (SPEC 51.2's own history).
3. **A dispatcher that takes no general register** (`drv_svc_call_x`
   2537-2570, `drv_stamped` 2495-2519). BP carries the selector. The segment
   travels on the stack, so the dispatch is re-entrant per task. The
   `pop word [snd_inst]` comes after `pop ds` (51.2.4). These are the right
   rules, and each of them was earned by a field bug.
4. **`drv_call` refuses a stray row before it touches DS**
   (2399-2440). A row whose `DRVR_SEG` is 0 also gets CF=1 / AX=0, which is
   the same answer `drv_svc_call` gives for "nothing publishes it". One
   refusal shape for every door.
5. **The attach and the parser are not resident.** `DRV_ATTACH` is a macro
   expanded in three images (3790-4135), and `CFG_MAP`/`CFG_DATA`/
   `CFG_SHARED`/`CFG_BOOT`/`CFG_SAVE` are written once and expanded into the
   blob and CTRL.DRV (51.5.3). The resident part is load, check, bss and
   release. That is the shape of a DOS `INIT` that is discarded after boot,
   applied to the kernel's own driver code.
6. **`DRV_H_KEEP` is DOS's INIT break address done right** (3834-3859): it
   uses a header word that must already be zero, so an older driver or
   kernel cannot say it by accident, and it costs no resident byte.
7. **Attach, then READY** (51.2.2). No fence answers from inside attach, so
   a refused attach cannot leave behind a worker or a volume. Only GEOS (its
   `DR_INIT` then use) and OS/2 (`INIT` at ring 0, then normal entry) draw
   this line as cleanly among the peers.
8. **Failures are banked in the row and reported after the first paint**
   (`drv_boot_x` 5784-5993, `drv_notice_x` 6074). The boot never stops on a
   driver, and the row's caption and the boot notice share one string
   source.
9. **The kernel relocates driver images and drivers declare nothing**
   (`mem_region_reloc` from `drv_load_at` 3533-3549). Classic Mac OS needed
   `dNeedLock` and handles. Here the kernel owns every word that names an
   image, which is the more robust split.
10. **SYSTEM.CFG is keyed records with a per-key version** (690-860). An
    old record of a key whose meaning changed stops matching instead of
    being misread. The reader is bounded by the byte count the read returned
    (`_deser` 5060-5135). Both properties are better than a positional
    struct, and better than INI's untyped strings for this use.
11. **The sound router is atomic** (snd.inc `snd_tone_req` 290-346). The
    priority compare, the port writes and the owner record are written in
    one cli window, with a `snd_live` boot gate.
12. **`OSAPI_DRV_CALL`'s fence is a published cell, not a class rule**
    (`DSV_PKGCALL`, 6604-6609). A package can reach `HDD.DRV`'s two verbs
    and never its `DSV_BLK`.

## Findings (ranked)

### drivers-1 - bug, high: one global worker count makes every unload wait for every driver's workers

`drv_wcnt` (driver.inc:6138) is one byte for all drivers. Spawn adds 1
(4599) and every exit subtracts 1 (4624). `drv_unload_x` sends `DRVV_DETACH`
and then waits up to 91 ticks for `drv_wcnt` to reach 0 (4171-4189). On
timeout it takes `jmp short .out`, which skips `drv_release`: the row stays
loaded, its image stays claimed and **its class stays published**, even
though the driver has already been detached.

`USBMOUSE.DRV` spawns a worker at READY that lives as long as the driver
does (usbmouse.asm:139-149, 162-167). So on a machine with the CH375 mouse
loaded:

- Unticking any other attached driver (Sound, Hard Drive, Ethernet, Ram
  Disk, os88net) freezes the click for about 5 s. The driver is then
  detached but still published, so `snd_tick` keeps far-calling the detached
  SOUND.DRV's `DSV_TICK`, and package doors still reach it.
- ctrl.inc's click then sets `DRVR_WANT = 0` and `DRVR_ERR = DRVE_OK`
  whatever happened (ctrl.inc:7140-7146). The page shows a loaded row that
  is unticked.
- A second click on the row sends a **second DETACH** to a detached driver.
  A driver that restores a saved vector restores a stale one.
- `drv_shutdown_x` (4208) unloads rows in order, and every row before the
  USB mouse's (rows 0-4 on kern_big) waits the full 5 s. A reboot with Sound
  and Hard Drive loaded takes about 10 s longer.

The same global count also pins every driver image and every `MC_DMA` head
for the compactor (memory.inc:2127). NO-PINNING-PLAN.md K1b deferred the
per-row count "until a second driver spawns". That has now happened.

**Recommendation (now).** Count workers per row. At spawn, `drv_pub_seg`
already has the segment, and `drv_row_ix_of` (3209) turns it into a row.
Exit already calls `drv_owns_seg_x`. Keep a `DRV_MAX`-byte table in `.bss`
indexed by row. `drv_unload_x` and memory.inc's image pin test that row's
byte, and the global count stays only as the sum the `MC_DMA` pin wants (or
K1a deletes that check). On timeout, also clear the class's publication
(disarm before you leak), so a detached driver is never dispatched.
**Bytes: about +25 `.cold`, +12 `.bss` resident.** Listed as NO-PINNING-PLAN
K1b, which estimated ~45 + `DRV_MAX`. The new evidence is the USB mouse's
persistent worker and the unload and reboot failure this causes, which K1b
did not consider.

### drivers-2 - hazard, medium: a driver can be unloaded while another task is inside it

`drv_pkg_call_x` is documented as worker-safe (6556-6574). Stream verbs 1, 3,
5, 6 and 7 are any-task (apps/os88api.inc:567-579). Neither path holds
anything that `drv_unload` honours:

- `drv_svc_call_x` tests `[drv_fseg]` and then loads it (2538-2543). A UI
  task that runs `drv_release` between those two instructions sends the
  worker to `0:000C`.
- More likely: a package worker preempted *inside* a socket or stream verb
  while the user unticks the driver. Detach and free then run while the
  worker's CS still points into the image.

The nest frames already count calls into images from non-UI tasks
(`wm_tcnt`, wm.inc:11737-11771). `drv_unload_x` never looks at them.

**Recommendation (now).** Before DETACH, wait (bounded, like the worker
wait) for `[wm_tcnt]` to reach 0. If it does not, refuse the unload with
`DRVE_BUSY` instead of detaching. Done exactly, this is a per-image entry
count, which the HEAP-UNPIN-PLAN measured at about 15 bytes a site. The
cheap form reuses `wm_tcnt` and is coarse but safe. **Bytes: about +12
`.cold`** for the coarse form.

**Prior art.** GEOS `GeodeUseDriver`/`GeodeFreeDriver` and AmigaOS
`OpenDevice`/`CloseDevice` (with `Expunge` only at a zero count) both
reference-count. Windows 3.1's MMSYSTEM installable drivers will not free a
driver that still has a `DRV_OPEN` outstanding. os8088 has no open count at
all.

### drivers-3 - bug, medium: `OSAPI_DRV_CLASSK` reports a loaded driver in the HMA as not loaded

On kern_big with `HMA_DRV` (the default whenever `ROM_COLD` is on;
kernel.asm:1167-1175), `%1_hma` places sound, hdd and net (Makefile
`HMADRV`) in the HMA pool and zeroes `DRVR_PARA` (driver.inc:3941). The
non-mega arm of `osapi_drv_classk_x` then derives "loaded" from the KB figure
(`cmp ax, 1`, 2216). A driver in the HMA that holds no heap claim sums to 63
paragraphs, which shifts to 0 KB, so CF=1 reads as "nothing of that class
is loaded". `HDD.DRV` claims nothing while it works (`DRVM_HDD`, 1726-1731),
and an idle sound card holds only its image.

Consequences on a 286+ with COLDHMA.SYS:

- INSTALL's `in_todrive` (install.asm:1605) concludes the disk driver is
  down and takes `DRVU_UP`, which reads CTRL.DRV off the system disk ("a
  floppy load of two files") for a driver that is already up.
- `in_netmount`'s test does the same for its class.
- DOS's Memory page greys a Sound or Hard-drive box (dosset.inc:589-641).
  The figure of 0 is arguably right for DOS. The predicate is not, by the
  slot's own contract.

kern_mega already fixed exactly this (2206-2214, KERN-MEGA-PLAN 14.88).

**Recommendation (now).** Use the row count (`inc cx` / `cmp cx,1`) on every
`OS88_DRIVERS` build, not just kern_mega. **Bytes: about +5 `.cold`** on
kern_big and kern_emu. Then fix SPEC 51.12.3 (see the mismatch list).

### drivers-4 - hazard, medium: `DSV_RELINST` runs at IF=0 into a driver that makes kernel calls

`snd_release_inst` opens `pushf/cli` (snd.inc:479-480) and calls
`drv_svc_call` with `DSV_RELINST` inside that window (485-495). For
SOUND.DRV this is `snd_release_both` → `sbl_release_inst` →
`sbl_stop_stream` → `sbl_unpin` (`OSAPI_MEM_MOVABLE` or a free) and
`sbl_pool_put` (`OSAPI_MEM_FREE`). In high-speed mode it also runs two DSP
resets of up to `SBL_POLLS` (4,096 ISA reads, "~4-8ms") each
(sb.inc:1897-1955, 2597-2622, 3001-3023). The driver's own comment says
these run "Outside every cli window: both are kernel calls" (sb.inc:1966).
The kernel's caller breaks that assumption. Each instance teardown that held
a stream can therefore run with interrupts off for tens of milliseconds,
long enough to overrun a serial mouse byte (CLAUDE.md's third defect).

**Recommendation (now).** Move the `OS88_DRIVERS` block above the
`pushf/cli`. `drv_svc_call` already refuses when nothing is published, and
`drv_fseg` is `.text` initialised to 0, so the call needs no `snd_live`
gate. Only the speaker half needs the cli window (SPEC 34.3). **Bytes: 0**
(a reorder).

### drivers-5 - weakness, medium: "PRESERVE BX" is a driver rule that the kernel could enforce for free

SPEC 51.2's ATTACH contract says "PRESERVE BX: drv_attach reads the row
through it on return". `%1_att` uses BX right after both of its driver calls
(`mov es, [bx+DRVR_SEG]`, 3834; `mov [bx+DRVR_ERR], al`, 3878). `drv_call`
saves nothing. USBMOUSE.DRV has already been bitten by this: an attach that
spent BL "wrote its DRVE_BUSY through a garbage row pointer"
(usbmouse.asm:115-123). `drv_cp_closed`'s THMPAL leg relies on the same rule
(`drv_call_ent` and then `drv_release` with BX, 3094-3097).

**Recommendation (now).** Add `push bx`/`pop bx` in `%1_call` (4130), which
is expanded in the blob, CTRL.DRV and HIBER.DRV, and in the THMPAL leg. Then
delete the rule from the contract: a rule a foreign driver must remember is
one it will forget. **Bytes: 0 resident, +2 in each of three images, +2
`.cold`.**

### drivers-6 - hazard, medium: an image is freed with no check that its vectors were unhooked

The contracts are "attach hooks nothing if it refuses" and "detach unhooks
everything" (os88drv.inc:44-53). Nobody verifies either. `drv_release`
frees the image (4317-4351) even if IRQ vector 08h-0Fh or 70h-77h still
names its segment. The next claim in that memory then runs as an ISR. This
is DOS-grade trust in third-party code, the area where DOS and Windows 3.x
were weakest.

**Recommendation (now).** After DETACH, or after a refused ATTACH, scan the
16 hardware IRQ vectors for the image's segment. `drv_hi_ivt`'s loop
(4420-4467) has the shape. If any vector matches, leave the image claimed
(a leak, never a crash) and bank a code. **Bytes: about +25 `.cold`.**

**Rethink (flag day, see drivers-15).** A kernel IRQ registry, `OSAPI_DRV_IRQ`
set and unset. The kernel would then own the IVT entry, refuse a line
another driver holds (so the kernel computes `DRVE_BUSY` instead of every
driver), force-unhook at release, and let the image move without scanning
the IVT. That is the same contract ABI-BREAK-LIST L3 already wants for
kern_mega's `mega_null0`/`mega_db`. Prior art: OS/2 1.x
`DevHlp_SetIRQ`/`UnSetIRQ` (with a sharing flag), Windows 3.x VPICD,
AmigaOS `cia.resource`/`misc.resource`.

### drivers-7 - weakness, low-medium: `drv_vol_bank` keeps one static pair, and the bracket does nest

`drv_vol_bank` stores into `drv_vol_cwd`/`drv_vol_drv` (drvvol.inc). The
design rule is "no two brackets ever nest", and deskmod.inc:79-100 already
works around a case where they do by pushing the outer pair by hand. Any
path from inside `drv_load_at`'s bracket (an attach or READY) that reaches
`mod_need` or `desksc` nests a second time. The outer `drv_vol_back` then
restores to the system volume instead of to where the user was standing.

**Recommendation (now).** Have the bank push the pair onto the caller's
stack and the back pop it (the `kentc_*` idiom). **Bytes: about 0 resident**,
and the 14-byte workaround in deskmod goes.

### drivers-8 - hazard, low (PLAUSIBLE, kern_mega): `drv_fs_hi` wraps its buffer offset at 64KB

`drv_fs_hi` advances the pool buffer as `add [dfh_buf], cx` (2786) with no
selector step. A `FSV_READ` whose 32-bit capacity `DI:CX` runs past the
selector's 64KB therefore wraps and overwrites the start of the buffer.
Bounded today only if no caller passes a selector buffer larger than 64KB.

**Recommendation (record).** Clamp the capacity to `10000h - BX` on entry, or
assert the caller rule in SPEC 62.9.2.4. **Bytes: +~8 `.mega`/`.cold` on
kern_mega only.**

### drivers-9 - size, medium (flag day): sound-only cells are copied for every class

`drv_svc` is 7 copies of 36 bytes, 252 bytes of resident `.bss`
(driver.inc:6173). Seven cells are read only for class 1: FM, STREAM, TICK,
RELINST, NAME, TONE and TIERS, 14 bytes. `DSV_BLK` (disk only) and `DSV_FS`
(file classes only) can never both be published, so they could share one
cell.

Reorder `DSV_*` so the common cells come first: CAPS, BLK/FS, the seven CP
cells and PKGCALL, 20 bytes. Sound's extras would follow. Class 1's copy
would be 34 bytes and the others 20. `drv_svcix` already holds per-class
offsets, so only its values change, and `%1_publish` clamps the copy to the
slot's length.

**Bytes: about -98 `.bss` resident** (252 → 154), and about +6 of
publish code in each of the three images. **Needs `DRV_VER` +2**, because
every driver's table offsets move. Batch it with D1 (DRVC renumber), whose
`drv_svcix` change it shares. Not listed. D1 prices only the index.

### drivers-10 - size, low-medium (now): driver file names are resident for loaders that are not

The class rows' `.DRV` names (driver.inc:1179-1226, about 100 bytes of
`.text`) are read only by `drv_load_at`, whose callers for class rows are
all images (the blob, CTRL.DRV, HIBER.DRV). This is kernel size pass 14's
title move one field over. Make `DRVR_FILE` an offset into an
image-expanded `DRV_FILES` table, like `DRV_TITLES`, and have the caller
stage the name into `cp_sbuf`, as `CFGN_STAGE` already does. The overlay
rows (xm/ss/thmv) keep their own names.

**Bytes: about -95 resident `.text`, about +100 in each of the three
images.** Two of those images are on-demand and the blob is transient.

### drivers-11 - consistency, low: dead `%else` arms and kern_small prose inside the driver gate

Everything from 1141 to 3396 and from 3398 to 6720 is `OS88_DRIVERS`, which
means `KERN_BIG`, which means `DRV_PCIETH`. The following never assemble on
any build:

- the `%else` of `drv_cls_svc_x` (1941-1954), a retired "compacted index"
  scheme;
- `DRV_CALLMAX equ DRV_MAX` (1635-1637);
- `drv_load_at`'s `DRVE_TWICE` arm (3460-3462);
- the nested `%ifdef KERN_BIG` / `%ifdef OS88_DRIVERS` (1189, 1205, 1326,
  1441, 1549, 3567, 3588).

`CFG_DATA`'s kern_small arm (1025-1031) and `CFG_NB`'s (867-875) are only
ever expanded inside the gate (5393, 6273). The DRV_MAX=4 essay (119-138)
describes a kern_small SYSTEM.CFG bit layout for a kernel that reads no
SYSTEM.CFG.

**Recommendation (now).** Delete the dead arms and their prose. **Bytes: 0.**

### drivers-12 - weakness, low: `drv_boot_x` writes CH and nothing reads it

`xor ch, ch` and `mov ch, 1` (5799, 5820) have no reader. Every walk uses
CL or CX. `drv_boot_want`'s "CH is 0 in each" (5994) is also false after a
successful settings read. **Bytes: -4 `.boot2`** (transient).

### drivers-13 - perf, low: each sound dispatch pays a .text→.cold hop

The FM, stream and tone path is: X stub → `osapi_snd_fm` → `snd_req_inst`
→ the .text thunk `drv_svc_call` (kernel.asm:8312) far into .cold →
`drv_svc_call_x` → `wm_nest_pushd` (pushf/cli/4 pushes) → far call →
`PKG_DISP` `call bp` → the driver → `wm_nest_pop`. Estimated at about 300
cycles (about 65 µs at 4.77 MHz) per FM verb. A 9-voice tune that sends
note-on and note-off at 30 Hz spends about 4% of an XT in dispatch alone.
`DSV_TICK` takes the same thunk 18.2 times a second inside IRQ0.

`drv_svc_call_x` is about 40 bytes. Placed in `.text` it would drop one far
call and its `retf` (about 50 cycles) from every sound dispatch.
`drv_pkg_call_x`'s own header gives the same "it is in .text on purpose"
argument (6551-6554). **Bytes: about -4 resident** (the thunk goes),
`KERN_CODE_MAX` +36. Measure before deciding (PERFORMANCE.md rule 4).

### drivers-14 - consistency, low: `OSAPI_FSX_SPK` ignores the 34.9 interlock that its close announces

`osapi_snd_play` asks `snd_str_busy` with DL=1 before taking channel 2
(snd.inc:933-936). `osapi_fsx_spk`'s open does not (1082-1120). Its close
runs through `spk_pcm_idle`, which tells the driver DL=0 (snd.inc:262-266),
announcing the end of an exclusive use it never announced. A `PCM_BG`
stream that is already playing is not refused at the bracket's open.

**Recommendation (now).** Either ask at the open (IF=0 is the caller's, and
`snd_str_busy` is a short far call) or state in SPEC 34.11 that the bracket
coexists with a stream. **Bytes: +~8 `.text`, or 0.**

### drivers-15 - rethink, medium (flag day): a class is both a service type and a single slot

One publication slot per class forces one driver per class. File
redirection now spends four class numbers on it (5 FILE, 8 FILE2, 9 CD,
10 HTTP), each a new ABI constant plus 42 resident bytes. ETHER.DRV and
PCIETHER.DRV are mutually exclusive because they share `DRVC_NET`
(`DRVE_CLASS`). Volumes already record which class serves them
(`DV_CLASS`).

A table of N attached-driver slots, each tagged with a *type*, would let
any number of file drivers coexist with no new constants. A volume would
record its slot. A package would ask for "the first slot of type T". This
is how MMSYSTEM enumerates wave devices (`waveOutGetNumDevs`), how the Mac
unit table works, and how the DOS device chain works.

**Bytes: about neutral to -40** (`drv_svcix` goes; the fptr/owner/svc arrays
are sized N instead of `DRVC_MAX`). Breaks every driver header,
`OSAPI_DRV_CALL`'s BH, and `DV_CLASS` users. Pair it with D1 and drivers-9.

### drivers-16 - weakness, low: the private ABIs are versioned by hand and outside the lock

`VX_ABI_VER` says "bump on ANY change to the contract below"
(vxabi.inc:14). The block exports 29 pointers into kernel state
(`VXB_*`). Nothing fails the build if a pointed-to field changes its
meaning or `vxabi.inc` changes without a bump. `tools/abi.lock.json`
fingerprints only `PKG_FMT` and `DRV_VER`. `XM_ABI_VER` and `HD_ABI_VER`
are in the same position. t_mirror guards XMEM's mirrored *numbers*, not
their meaning.

**Recommendation (now).** Have `tools/abilock.py` fingerprint `vxabi.inc`
(and the XMEM and HDD ABI blocks) against their version constants.
**Bytes: 0** (host tool). Related consistency note: `vxabi.inc` is shared
*source* for both sides, while XMEM's ABI is a pair of mirrors
(kernel/xmem.inc:63-97 and xmem.asm:110-143). The shared-source shape is
the better one. ABI-BREAK-LIST P2 says these move freely, but nothing
checks that a move bumped the version.

### drivers-17 - weakness, low (record): SYSTEM.CFG drops records it does not know at every save

Rule 1 (driver.inc:704-710) loses newer settings when a disk is booted
alternately by an older and a newer kernel (an update through The Wire, or
a live medium against a floppy), and also between kern_emu and kern_big
for keys only one of them has. Windows INI files and Mac preference
resources keep keys they do not understand.

A copy-through of unknown records in `cpc_ser` would need the old file
re-read at save. **Bytes: about +40 CTRL.DRV, 0 resident.** Record it. The
current behaviour is a deliberate choice.

## Rethinks

- **Lifetime by count, not by hope** (drivers-1, drivers-2). Every
  successful peer that unloads drivers counts its users: GEOS, AmigaOS and
  MMSYSTEM. os8088 counts workers globally and calls into a driver not at
  all. A per-row byte that counts workers plus in-calls is the whole model,
  and the nest frames already pay for most of it.
- **The kernel owns the IRQs** (drivers-6). It fixes four things at once:
  the vector left behind after a detach, a `DRVE_BUSY` the kernel computes
  itself, image moves without an IVT scan, and kern_mega's IRQ fixups (L3).
- **Slots, not classes** (drivers-15), together with **a common-prefix
  service table** (drivers-9). One flag day with D1 and N1 makes the driver
  table smaller and makes "a new file driver" mean no new ABI number.
- **Not recommended:** a DOS-style request packet in place of
  verb-plus-table. The direct near-proc table is cheaper on an 8088 than
  building a packet, and the IRQ0 read depends on it.

## Prior art (systemic)

| system | model | better than os8088 | os8088 better |
|---|---|---|---|
| MS-DOS | CONFIG.SYS device chain, strategy/interrupt entry, request packets; INIT returns the break address; never unloaded | INIT's break address is exactly `DRV_H_KEEP`, so the idea is proven | load and unload at runtime, typed classes, a failure banked instead of a hang, no request-packet build per call |
| Windows 3.0/3.1 | installable drivers (`DriverProc`: `DRV_LOAD`/`ENABLE`/`OPEN`/`CLOSE`/`CONFIGURE`), VxDs, VPICD, SYSTEM.INI `[drivers]` + per-driver sections | open counts; many devices per type (MMSYSTEM IDs); IRQ virtualization; INI keeps unknown keys | the per-row blob is typed and versioned; the CP page ABI (`DSV_CP*`) matches `DRV_CONFIGURE` at a fraction of the cost |
| OS/2 1.x | DevHlp services: `SetIRQ`/`UnSetIRQ`, `Lock`/`Unlock` of physical memory, `ProcBlock`/`ProcRun`, `AttachDD` IDC, init-only code discarded | the kernel owns IRQs and enforces sharing; memory locking is explicit per buffer | far smaller; the movable image with the kernel holding the relocation is simpler than locking |
| Mac System 5-7 | Device Manager DRVR, unit table, Open/Prime/Control/Status/Close, `dNeedTime`; cdev Control Panels (`initDev`/`hitDev`/`closeDev`/`keyEvtDev`) | multiple units per driver type; `dNeedTime` gives periodic work in task context rather than inside the timer interrupt | the `DSV_CP*` page ABI *is* the cdev model, and is cleaner; a driver needs no `dNeedLock` because the kernel moves the image itself |
| GEOS | drivers are geodes; `GeodeUseDriver`/`GeodeFreeDriver` reference counts; `DR_INIT`/`DR_EXIT`/`DR_SUSPEND`/`DR_UNSUSPEND`, where suspend can refuse with a reason | reference counting; a refusable suspend with a reason. The closest peer, on the same CPU | `OSAPI_DRV_SUSPEND` (51.11) is the same idea, with the hardware facts recorded on the way past (`DRVV_HWINFO`) |
| AmigaOS | exec devices and libraries with open counts, `Expunge` deferred to low memory; resources for CIA, serial and parallel ports | open counts plus lazy unload; resource arbitration | 8088-sized; no message-port overhead per request |

Cheap to adopt: GEOS-style counting (drivers-1, drivers-2, about 40 bytes);
OS/2-style IRQ ownership (drivers-6, about 25 bytes now, the cell at a flag
day); MMSYSTEM-style device IDs (drivers-15, flag day).

## Doc/code mismatches

Each entry gives the file and line, the current text, and what the code does.
The proposed text is in the structured output.

1. kernel/snd.inc:9 says "THE PC SPEAKER IS THE ONLY SINK. The Sound
   Blaster and OPL2 drivers, the". In the code, `snd_tone_out` routes to the
   driver's `DSV_TONE` (135-145), FM and stream dispatch to SOUND.DRV
   (727-840), and the tier is `snd_route`.
2. kernel/snd.inc:42 says "PIT channel 0 is never touched here at all." In
   the code, `spk_off` reloads ch0 (`sch_pit0`, 1056) and `osapi_fsx_spk`
   trims `[sch_rdiv]` (1118).
3. kernel/snd.inc:22 says "and ch0 is never written." That holds for the
   clip pacer, which now lives in apps/os88pcm.inc, and is false for the
   bracket door.
4. kernel/snd.inc:557-560 says "runs the probes the driver". `snd_init`
   stores .bss and sets `snd_live`. It runs no probes.
5. kernel/snd.inc:646 says "The three API slot targets (SPEC.md 20.3) -
   caps, tone and the exclusive". The file holds caps, tone, play, FM,
   stream and `OSAPI_FSX_SPK`, and routes through `snd_rt_card`.
6. kernel/driver.inc:153 says "A SECOND block-serving class beside
   DRVC_DISK". ETHER.DRV publishes `DSV_BLK` = 0 (ethdrv.inc:41). The class
   serves sockets through `DSV_PKGCALL`.
7. kernel/driver.inc:261 says "dw: -> its display name (.text)". The field
   is an offset into the image's `DRV_TITLES` (1170-1176, 5917-5918).
8. kernel/driver.inc:387 says "drv_bss GROWS when the round-up does not",
   and 3511 says "drv_bss GROWS instead, once it". `drv_bss` refuses
   (`ja drv_check.bad`, 3753). Its own header says growing "IS WORSE".
9. kernel/driver.inc:119-138 (the DRV_MAX=4 essay) says "kern_small's
   SYSTEM.CFG bit for the cable is 2 where". kern_small loads no driver and
   reads no SYSTEM.CFG (SPEC 51.0, stub 6863).
10. kernel/driver.inc:1026 says "the preset round-trips there even though".
    That arm is never expanded.
11. kernel/driver.inc:1821 says "...ditto, on kern_small (no PCI row)". This
    is inside `OS88_DRIVERS`, and every build that assembles it has the PCI
    row.
12. kernel/driver.inc:2054-2057 says "The strings it hands back (drv_errstr,
    ... STAY in `.text`". They are TOASTSTRs in `.modu`, read
    `[cs:bx+drv_errstr]` and staged (2087-2128).
13. kernel/driver.inc:2193 says "SERIAL.DRV are under one". SERIAL.DRV is
    `ser_row` (serial.inc:12), not a `drv_tab` row. SPEC 51.12.3 says the
    same thing.
14. kernel/driver.inc:2386-2389 says "four of the five sites in the tree
    (drv_attach x2, drv_tier_x and blank.inc's ss_call)". The callers are
    `drv_unload_x` (4168), `drv_cp_closed` (3096) and blank.inc:656.
    `%1_call` uses `drv_call`, and `drvf_drv_call` is gone.
15. kernel/driver.inc:4128 says "%1_call - drv_call_ent:". The body loads BP
    itself and far-hops to `drv_call`.
16. kernel/driver.inc:4545 says "`ES == [drv_fseg]` is an identity test".
    `drv_pub_seg` walks every class (2002-2018).
17. kernel/driver.inc:5298 says "(84,064 bytes on kern_big)".
    build/kernel.sys is 85,168.
18. kernel/driver.inc:5994 says "(CH is 0 in each)". CH is 1 after a
    settings read, and nothing reads it.
19. kernel/driver.inc:6551-6554 says "IN .text ON PURPOSE ... the cold half
    of this module would need a shim in each direction", and 6574 says
    "five sites below name drv_fptr..drv_fptr5 outright." The routine makes
    three far calls into `.cold` (6581, 6642, 6649), and the far call is one
    site, `drv_pkg_disp` (6693).
20. drivers/os88drv.inc:74 says "rounded up to a KB,". The kernel rounds to
    512 (driver.inc:3836-3840).
21. drivers/os88drv.inc:95 says "the kernel never loads one and drv_check
    would refuse it". `drv_load_at` loads `DRVC_OVL` rows and stops after
    the bss (3567-3571).
22. drivers/os88drv.inc:620 says "YOUR DETACH MUST NOT RETURN UNTIL YOUR
    WORKER IS GONE". `drv_unload` waits on `drv_wcnt` (4171-4189), and
    USBMOUSE detaches at once.
23. SPEC.md:102098 says "published (`ES == [drv_fseg]`)", and 102117-102120
    say "Spawn from a service call" and "must not return until the worker
    is gone". The code is as in items 16 and 22. READY is the documented
    spawn point.
24. SPEC.md:101105 says "`drv_snd_sniff` are `.ovlw`; `drv_boot` is
    `.ovl`". `drv_init_x` and `drv_sniff` are `.ovl` (6795-6800) and
    `drv_boot_x` is `.boot2` (6858).
25. SPEC.md:101395 says "the strip stops at the image's KB rung and the
    claim is". The strip stops at the 512-byte granule (51.1.2 itself).
26. SPEC.md:101447 says "(`HDD.DRV` does: 34, its table stopping before
    the". `hd_services` publishes `hd_pkg` at `DSV_PKGCALL`, so its length
    is 36 (hdd.asm:1542-1573).
27. SPEC.md:101543 says "driver bitmap is one bit per row index". The
    bitmap is mapped through `%1_bit` (1081-1110).
28. SPEC.md:101561 says "kern_small, which has no second `DRVC_NET` row,
    keeps". kern_small loads no driver, and `DRVE_TWICE` is dead in
    `.clsfree`.
29. SPEC.md:185223 says "`Hard drives (Up to 32K)`". `DRVM_CEIL_DISK` is 4.
30. SPEC.md:185295 says "Every call site in the tree goes through DOS's",
    and 185302 says "because `drv_load` writes `DRVR_SEG` and". INSTALL (4
    sites), Redline and EMS.DRV call the slot directly, and `DRVR_PARA` is 0
    for an image in the HMA or above 1MB.
31. SPEC.md:71046 says "The API surface is five §20.3 slots". `OSAPI_FSX_SPK`
    is a sixth, in snd.inc.
32. drivers/xmem/xmem.asm:84-85 says "drv_load_at claims mem_bytes_kb_x of
    the file's". The claim uses `mem_bytes_gp_x` (driver.inc:3501), 512-byte
    granules.
33. docs/plans/NO-PINNING-PLAN.md:177 says "Only `SOUND.DRV` spawns a task
    (`OSAPI_DRV_TASK` has one caller)". USBMOUSE.DRV spawns too
    (usbmouse.asm:144).

## Open questions

1. Can anything inside `drv_load_at`'s volume bracket (an attach, READY, or
   a page draw at boot) reach `mod_need` or `desksc`'s bracket? This decides
   whether drivers-7 is live or latent.
2. Does any caller pass `FSV_READ` a pool selector whose capacity exceeds
   64KB less the offset (drivers-8)?
3. With SOUND.DRV in the HMA, its ISR and `DSV_TICK` are reached from
   interrupts. Is A20 guaranteed on at every IRQ, including straight after
   an `int 15h AH=87h` on an AT BIOS? This belongs to the memory/ROM area,
   flagged here because the driver placement depends on it.
4. drivers-14: should the fullscreen speaker bracket refuse while a
   Sound Blaster stream plays, or is overlap intended?
5. drivers-1's fix also decides the K1a/K1b question in NO-PINNING-PLAN.
   Does the owner want the global count kept for the `MC_DMA` pin, or
   deleted as K1a proposes?

## Verification

Adversarial pass, 2026-11-08: every bug, hazard, high and medium finding
re-read against the code (both build arms where they differ), and every doc
mismatch re-quoted from its file. Nothing refuted. The applied doc fixes are
in `data/drivers.json`.

### Findings

- drivers-1 (high bug, global drv_wcnt): CONFIRMED. The timeout skips
  `drv_release`; `usbm_row` is row 5, so rows 0-4 each pay the wait at
  reboot; K1b's deferral reason is stale.
- drivers-2 (hazard, unload while a task is inside): CONFIRMED. Race by
  inspection, not reproduced.
- drivers-3 (bug, CLASSK and the HMA): CONFIRMED. Only where `.cold` really
  is in the HMA and `ROMF_HPLACE` succeeds.
- drivers-4 (hazard, DSV_RELINST at IF=0): CONFIRMED. Duration corrected:
  normally well under 1 ms, several ms worst case on a high-speed DSP reset,
  not "tens of ms".
- drivers-5 (PRESERVE BX): CORRECTED. `drv_unload_x` relies on the rule too.
  The `.cold` sites are resident and the blob is documented as full to the
  byte, so it is not "0 resident". Severity lowered to low.
- drivers-6 (hazard, IRQ vectors unchecked): CONFIRMED.
- drivers-7 (static volume bank pair): CONFIRMED. Latent.
- drivers-8 (kern_mega `drv_fs_hi` wrap): CONFIRMED as a record item. Line
  :2786.
- drivers-9 (common-prefix DSV table, -98 .bss): CONFIRMED. The arithmetic
  holds. Not already in ABI-BREAK-LIST or KERNEL-BYTE-QUEUE.
- drivers-10 (resident .DRV names): UNCHECKED. The saving needs pricing.
- drivers-11 (dead arms in the OS88_DRIVERS gate): CONFIRMED by a nesting
  walk. The prose is fixed in this pass and the arms are left for phase one.
- drivers-12 (dead CH stores): CONFIRMED. Lines 5799 and 5820.
- drivers-13 (.text-to-.cold hop): CONFIRMED. The structure is as described;
  the cycle figure is an estimate.
- drivers-14 (FSX_SPK skips the stream interlock): CONFIRMED.
- drivers-15 (class = slot rethink): CONFIRMED as a design item.
- drivers-16 (private ABIs unlocked): CONFIRMED.
- drivers-17 (SYSTEM.CFG drops unknown keys): CONFIRMED. Deliberate, so
  record only.

### Doc mismatches (all 33 CONFIRMED; 40 fixes after splitting and additions)

The quotes were re-checked character for character, and each replacement was
rewritten to fit the whole sentence it lands in. Two reviewer proposals would
have changed code: the `DRV_MAX equ 4` rewrite at driver.inc:125 and the
"delete the %else arm" at driver.inc:1026. Both were turned into comment-only
edits. Code-file fixes were machine-checked to leave every code token as it
was.

1. snd.inc:9 (the "only sink" paragraph): CONFIRMED. The whole paragraph is
   replaced, not just its first line.
2. snd.inc:42 (PIT channel 0): CONFIRMED.
3. snd.inc:557 (the probes): CONFIRMED.
4. snd.inc:646 (three slot targets): CONFIRMED.
5. driver.inc:153 (DRVC_NET is a block class): CONFIRMED.
6. driver.inc:261 (DRVR_TITLE): CONFIRMED.
7. driver.inc:387 (drv_bss GROWS): CONFIRMED.
8. driver.inc:3511 (drv_bss GROWS): CONFIRMED.
9. driver.inc:125 (kern_small DRV_MAX essay): CONFIRMED, made comment-only.
10. driver.inc:1026 (CFG_DATA kern_small arm): CONFIRMED, made comment-only.
11. driver.inc:1821 (DRVM_CEIL_NET on kern_small): CONFIRMED.
12. driver.inc:2054 (strings stay in .text): CONFIRMED.
13. driver.inc:2193 (SERIAL.DRV): CONFIRMED.
14. driver.inc:2387 (drv_call_ent's sites): CONFIRMED. The whole block is
    rewritten, since drvf_drv_call is gone too.
15. driver.inc:4545 (`[drv_fseg]` fence): CONFIRMED, along with the next
    sentence's `[drv_fseg]` and "spawn from a service call".
16. driver.inc:5298 (84,064 bytes): CONFIRMED (85,168 now).
17. driver.inc:5994 (CH is 0): CONFIRMED.
18. driver.inc:6574 (five sites): CONFIRMED.
19. driver.inc:4128 (`%1_call`): CONFIRMED.
20. os88drv.inc:74 (rounded to a KB): CONFIRMED.
21. os88drv.inc:95 (DRVC_OVL never loaded): CONFIRMED.
22. os88drv.inc:620 (detach rule): CONFIRMED, along with rule 1 (READY).
23. SPEC 51.7 (`ES == [drv_fseg]`): CONFIRMED. Rule 2 is a separate fix.
24. SPEC 51.0.2 (`.ovlw`/`.ovl`): CONFIRMED.
25. SPEC 51.2 (KB rung): CONFIRMED.
26. SPEC 51.2 (HDD.DRV's length 34): CONFIRMED.
27. SPEC 51.2.1 (bitmap per row index): CONFIRMED.
28. SPEC 51.2.1 (kern_small keeps DRVE_TWICE): CONFIRMED.
29. SPEC 51.12.1 (Up to 32K): CONFIRMED.
30. SPEC 51.12.3 (every call site through dos_classk): CONFIRMED. The HMA
    predicate sentence is left for the drivers-3 fix.
31. SPEC 34.7 (five slots): CONFIRMED.
32. xmem.asm:84 (mem_bytes_kb_x): CONFIRMED, along with "a byte of a KB" at
    :91.
33. NO-PINNING-PLAN:177 (only SOUND.DRV spawns): CONFIRMED.

Added by the verifier (same facts, other places):

- snd.inc:6-7, the banner's "three ... slot targets".
- driver.inc:1801, "Up to 32K".
- driver.inc:3708, "KB boundary" in drv_bss's header.
- SPEC 51.7 rule 2, which follows `sbl_detach`'s own comment that its wait is
  ordering, not the guarantee.
- SPEC 51.12.3, `SERIAL.DRV` and kern_mega's row-count arm.
