# Driver size pass 1: the record

Branch `end-of-week`, cut from `elendilon` at `3b16b60b` on 2026-11-15. The
brief is `BRIEF.md` beside this file, and each agent's note is the authority
on its own rows: `hdd.md`, `netfs.md`, `ether.md`, `sound.md`, `saver.md`,
`vesa.md`, `cdram.md` and `kmod.md`. Seven agents worked `drivers/`, and an
eighth (`kmod`) was added for the kernel's on-demand modules once `hdd`
finished.

The pass was stopped at the owner's word ("the week has rolled over"). Each
agent finished the item it was on, and its note lists what it did not reach.

## 1. What it bought

Figures are assembled from the merged tree. "Image" is what RAM pays while a
driver is loaded: the image as `os88drv` reports it, **including bss**,
which is the figure the agents' notes use. "File" is what the disk pays: the
lz4-packed file, with trailing bss off the disk.

| driver | image (RAM) | file (disk) | heap claim | agent |
|---|---:|---:|---|---|
| SAVER.DRV | 10,247 → 8,933 (−1,314) | 7,108 → 6,009 (−1,099) | 21 → 18 granules | saver |
| CDROM.DRV | 9,038 → 8,203 (−835) | 5,233 → 4,644 (−589) | 18 → 17 granules | cdram |
| VESA.DRV | 8,105 → 7,406 (−699) | 3,848 → 3,536 (−312) | 16 → 15 granules | vesa |
| PCIETHER.DRV | 17,076 → 16,509 (−567) | 10,918 → 10,599 (−319) | 34 → 33 granules | ether |
| ETHER.DRV | 17,389 → 16,848 (−541) | 11,106 → 10,829 (−277) | 34 → 33 granules | ether |
| HDDTOOL.DRV | 16,508 → 16,153 (−355) | 12,216 → 12,116 (−100) | 33 → 32 granules | hdd |
| SOUND.DRV | 10,478 → 10,265 (−213) | 8,988 → 8,889 (−99) | kept after attach −44 to −139 | sound |
| THMPAL.DRV | 2,575 → 2,402 (−173) | 2,259 → 2,133 (−126) | 6 → 5 granules | saver |
| NET.DRV | 4,185 → 4,161 (−24) | 3,473 → 3,471 (−2) | | netfs (the transport rewrite reverted, section 2.1) |
| HTTPFS.DRV | 5,553 → 5,513 (−40) | 3,606 → 3,584 (−22) | | netfs |
| XMEM.DRV | 2,037 → 2,015 (−22) | 1,878 → 1,864 (−14) | | vesa |
| HTTPPAGE.DRV | 2,350 → 2,339 (−11) | 2,005 → 1,995 (−10) | | netfs |
| HDD.DRV | 4,096 → 4,096 (resident span −32) | 3,571 → 3,558 (−13) | | hdd |
| EMS.DRV | 785 → 783 (−2) | 717 → 715 (−2) | | vesa |
| **drivers, total** | **−4,796** | **−2,984** | | |
| OS88NET.COM | unpacked 14,959 → 13,870 (−1,089) | 10,784 → 10,319 (−465) | | netfs + ether |
| RAMDISK, RAMPAGE, USBMOUSE, VMMOUSE | unchanged | unchanged | | cdram, vesa |
| the eleven kern_big modules | unchanged | unchanged | | kmod |

**Disk.** The 360KB system disk went from **37 to 41 free clusters**:
SAVER −2, VESA −1 and HTTPFS −1. On the disks that carry OS88NET.COM
(`network360`, `util360`, and the 720KB, 1.2MB and 1.44MB apps disks) it is
465 bytes smaller and stays at 11 clusters at 360KB. `python3
tools/os88diskuse.py --diff docs/plans/completed/dsp1/baseline.json` reproduces these
figures.

The snapshot was taken on a shallow clone with no build number. So
`KERNEL.SYS` and a few module files (FORMAT +1, FILECP +8, kern_small's
KERNEL.SYS +16) show build-number noise in that diff. They are not part of
this pass: `kmod` proved every kernel and module byte-identical across the
pass at one commit.

**Kernel.** Every kernel's resident image is byte-identical to the base,
with one exception: the Drivers page's per-driver memory figures in
`kernel/driver.inc`, which the fast-tier gate `drvmem` checks against each
image. ETHER went 34 → 33, PCIETHER 34 → 33, VESA 16 → 15 and
CDROM 18 → 17. `DRVM_CEIL_NET` went 34 → 33 in the kernel and in its SDK
mirror, `apps/os88api.inc`. That last one is a caption's ceiling, not a
cell, so `tools/abilock.py --update` ran **without a `DRV_VER` bump**,
following the precedent SPEC.md 72.4.3 records for 31 → 34.

## 2. Defects found

Fixed:

1. **VESA: `gfx_spans` drew nothing on a VESA desktop.** The span fill read
   its width after `vx_addr` had overwritten DX, so Paint's filled shapes
   and isoTron's sweeps came out wrong. The fix is smaller and faster.
2. **VESA: a 16bpp row fill lost its carry across a window edge** and
   wrapped onto the start of the same window (800x600x16, 640x480x16). The
   fix costs no bytes and no cycles.
3. **CDROM: every subfolder of a disc with audio tracks also listed the
   root's `TRACKnn.CDA`.** `cd_resolve` clobbered AX, so `[cd_cwd]` was 0.
4. **CDROM: a hostile TOC could make `CDV_PLAY` divide-fault**, and on a
   286 that hangs. `cd_msf` now adds the lead-in before it clamps.
5. **OS88NET.COM: DOS error 39 (disk full) reached os8088 as an I/O error.**
   Two instructions were swapped.

Not fixed (WANTS THE OWNER):

6. **SAVER, Geometric Shapes / Flow:** `sv_mulsh` clobbers AH, so every
   other point's latitude is its shrunken radius. Ring A's odd points sit at
   27 instead of 44, and ring B's at 47 instead of 0. Fixing it changes the
   picture.
7. **VESA: `vx_offer` accepts modes the class byte cannot name** (960x720,
   768x576, 1024x576), so the Display page's mode walk answers garbage for
   them. The fix costs about +6 bytes and changes what Auto picks on such a
   card.

### 2.1 A regression the pass made, and gave back

**netfs's rewrite of the cable transport (`drivers/net/lplink.inc`, with
`lplslv.inc`) made `doscable` intermittent.** It was the one failure in the
final pass. The symptom: os8088's DOS connects over the cable through
NET.DRV, but its request never reaches the far side and it takes no
listener.

`tools/os88bisect.py sample` placed it:

| point | `doscable` failed |
|---|---|
| base | 0/3 |
| hdd's tip | 0/4 |
| netfs's tip | 1/4, then 2/6 |
| hdd + netfs merged | 3/4 |
| netfs's tip with NET.DRV's own code back at base | 2/6 |
| netfs's tip with the transport back at base | 0/6 |
| the merged tip with the transport back at base | 0/6 |

netfs's harness had proved the transport identical against a scripted
partner on 22 scenarios, so what it changed is TIMING against MartyPC's
cable model. The likely cause is the shorter poll loop and the faster byte
send, a window the harness's partner never closes. Note that netfs could
not reproduce the failure in five of its own runs at its tip.

**Taken out:** the transport is back at its base. NET.DRV's own cold-path
savings stay. That gives back NET.DRV image +90 and file +77, and
OS88NET.COM +59. `DRVM_IMG_NET` is back at 9. **For a next pass:** the
rewrite (`ade2f28d`'s `lplink.inc` hunks) is worth about 77 file bytes, but
it needs a harness whose partner holds the real cable's timing, and a
sampled `doscable` (n ≥ 6) before it lands.

## 3. WANTS THE OWNER

| item | bytes | from |
|---|---|---|
| Fix defect 6 (changes the picture) | ~0 | saver |
| Fix defect 7 (changes the modes offered) | ~+6 | vesa |
| VESA's int 10h stack 898 → 768, the figure SPEC.md 120.3.4 documents | −130 image, less headroom | vesa |
| An early "not due" test ahead of `sv_entry`'s seven pushes | +8, −190 cycles a poll | saver |
| Can a CTRL.DRV settings-core saving move `mov bp, MODS_SIZE` (`kernel/mod.inc`, resident `.cold`, 2 bytes, same size)? Until this is ruled, a module pass is confined to `.modu` and the other modules | ruling | kmod |

## 4. Cross-file: for a second pass, or a shared lane

* **Giving back attach-only code after attach.** This needs a loader or
  `DRV_H_KEEP` change.
  - ETHER: 332 bytes attach-only, plus 326 run at attach and wake.
  - PCIETHER: 227, plus 265.
  - NET: 303, through the existing `DRV_H_KEEP`, once the driver's state
    moves ahead of that code.
  - HDD: 220, but no step today, because it is not at the tail.
  - Nothing for VESA, SAVER, THMPAL or the modules (VESA already overlays
    its attach code with its stack).
* **Shared includes:**
  - `os88ui_bhit` (23 bytes) and `gfxe_wline` (17 bytes) are assembled
    into SAVER but never called; each wants a knob.
  - `os88line.inc` has four relaxed jumps, 12 bytes in every includer.
* **OS88NET.COM** ships about 231 bytes of zeroed stack state that could be
  reserved instead of emitted.
* **Stale prose, not fixed:**
  - the Makefile comment at `saver.bin` (the sine table);
  - SPEC.md 9.11's VMMOUSE.DRV size (357; it builds 375);
  - several "four sockets" comments, where `NET_SOCKS` is 8;
  - the `snd_keepmix` banner, which says ~1.3KB of zeros ship (~310 now).

## 5. Not reached

Every note has a NOT REACHED list. The larger items:

* **kmod:**
  - About 10–22 bytes of `call X / ret` tails in CTRL's panel, EXTD,
    FSCHECK and HIBER (sites listed).
  - None of the bigger shapes in DISKOPS, STIKMENU or HIBER.
  - Its module-to-source map is the starting point for a module pass.
* **cdram:** RAMDISK and RAMPAGE were read and not changed; ideas priced in
  its section 10.
* **saver:** `sv_rot3` as a table-driven loop (~−20, needs a cycle count),
  and the fish band build's mask and step in registers (−20 to −30).
* **netfs:** a few 1–2-byte tails, a harness for NET.DRV's own changes, the
  look-boot.
* **hdd:** FSCHECK.DRV is a kernel module (`kernel/fsck.inc`), so it was
  left to kmod.

## 6. Verification

Every agent proved equivalence with a unicorn harness that ran its base and
tip images side by side. They compared registers, ports, kernel calls and
state, and drew device models where the driver needs them (8390, RTL8139,
SB DSP 1.x–4.x, OPL, MPU-401, ATAPI, a VBE BIOS). The harnesses were broken
on purpose to show they go red. The exception is netfs's NET.DRV and HTTP
drivers, which were checked by reading.

Hot paths are byte-identical or faster, with the counts in each note. No
transfer call was added anywhere. `make`, the fast tier, `make mega` and
`make emu` were green at each merge.

The final pass is the deduplicated union of every row the notes name: 156
soak rows and `megapkg`. Its results are in section 7.

## 7. The final pass

The run was `tools/os88soak.py` over the union of the 156 soak rows the
notes name, at `e01ce14c`: **150 ok, 1 FAIL, 6 SKIP** in 25 minutes.

* **The skips.** Five rows want QEMU 9 (`hmahiber`, `b4ether`,
  `b4ethswap`, `b4lease`, `hmadosret`), and `win16mscdex` wants the WIN16
  reference fixtures. Neither is on this box. Bochs, 86Box, the PicoMEM host
  firmware and `wiredisk` were installed for the run, so none of their rows
  skipped.
* **`megapkg`** (full tier) is ok.
* **The failure** was `doscable`. Section 2.1 tells it: the cable transport
  was reverted at `10af988a`.
* **The rows the revert could reach were re-run once** on the fixed tree,
  16 rows, **16 ok**: `doscable`, `socktest`, `fsclass`, `drvup`, `hmadrv`,
  `wirezone`, `thewire`, `wire`, `netinst`, `netinstbad`, `netcoexist`,
  `netpromise`, `romsock`, `sfx`, `dosnetarena`, `telnet`.

`test-full` was not run. `end-of-week` is a feature branch, and the full
tier is owed when this work first merges into `elendilon` (CLAUDE.md,
Testing).
