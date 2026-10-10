# Handoff: install from the SOURCE's installer, not the booted system's

**Nothing here is built, and this document changes nothing.** It is the
record of a defect found on 2026-10-07 at `111d6ba` while testing something
else (SPEC.md 18.91.5's `HDCYLRUN=1`), and the owner's direction for it:

> *"I'd rather it get handled by correctly installing from the disk, which
> likely involves invoking the installer from the source OS not the boot OS.
> That will also help prepare us for online upgrades."*

So this is not a request for a guard in today's installer. It is a design
handoff: **the program that writes a disk's boot sector must be the one that
came with the kernel it is installing**, and today it is not.

## 1. What happened - MEASURED, off the owner's 86Box disk

The owner installed a knob kernel (`make HDCYLRUN=1`) onto an ST-225 behind
an ST11M (86Box's 8088, the ST11 layout: physical cylinder 0 hidden for the
card's parameter record), booted C:, and the machine did not come up.

Read back off the image on the host:

| what | is |
|---|---|
| `KERNEL.SYS` on C: | the knob kernel, byte for byte (`fc16c677…`, the `HDCYLRUN` tree's) |
| `HDDTOOL.DRV` on C: | the knob tree's: the boot sector it carries is the knob one |
| the partition's boot sector, bytes 62..503 | the **stock** tree's `boothd.bin`, byte for byte - and 3 bytes off the knob tree's |
| `STREAM.DAT`, the listing | intact: every dword of 12.5 MB its own offset |

The three bytes are all one constant, `BLOB_SEG` - where the boot blob is
loaded, and so where the kernel's own LZ decoder (`kz_hd`) sits:

```
stock  00F3  BA 80 18        mov dx, 0x1880        ; load the blob here
       0105  9A 9E 08 80 18  call 0x1880:0x089E    ; ...and call its decoder
       0114  B8 80 18        mov ax, 0x1880        ; ...and tell the kernel
knob   00F3  BA C0 18        mov dx, 0x18C0
       0105  9A 9E 08 C0 18  call 0x18C0:0x089E
       0114  B8 C0 18        mov ax, 0x18C0
```

The blob moves up by 40h paragraphs because the knob build is 1 KB larger -
not for its own code but because EVERY knob build carries `DSK_OVLPAD`'s
1,024 bytes (kernel.asm), which is why `BLOB_SEG` is 6336 in every knob tree
and 6272 in the shipped one. A stock boot sector in front of it far-calls 1 KB
short, into the middle of the kernel image. Whatever the screen then shows is
whatever that code does; **none of the knob kernel's own code ran**, which is
why it says nothing about `HDCYLRUN` (whose read shapes VIDDISK's sweep
passed, 48 of 48, on the same disk).

## 2. Why: the boot sector is the RUNNING installer's, the files are the SOURCE's

- `hd_inst_vbr` (`drivers/hdd/inst.inc`) writes `hd_bootvbr`, which is
  `boothd.bin` **`incbin`'d into `HDDTOOL.DRV` at build time**
  (`drivers/hdd/partw.inc`, `hd_bootvbr`). Both install paths write it - the
  erasing one at the end of the system phase, the keep one (`hd_inst_keep`,
  SPEC.md 52.10.15) the moment the kernel lands.
- `HDDTOOL.DRV` is read off the **SYSTEM volume** - the one banked at
  `DRVV_READY` (`drivers/hdd/hdtool.inc`'s header) - which is the volume the
  machine BOOTED from. The install SOURCE only supplies files.
- So "boot C:, put the new system disk in B:, Install to C: keeping files" -
  which SPEC.md 52.10.15 describes and `tests/instdeep.py` exercises -
  writes the **old** boot sector beside the **new** kernel.

What in `boothd.bin` belongs to the kernel it boots (`Makefile`,
`BOOTHD_DEFS` and `KZDEF2`):

| define | from | moves when |
|---|---|---|
| `BLOB_SEG` | `kseg + ksize / 16`, the kernel's footprint | the footprint crosses a KB - most kernel changes, eventually |
| `SPL_FSEG` | the kernel symbol `spl_fseg` | the splash's font moves |
| `KZ_HD` | the kernel symbol `kz_hd` | the blob's layout moves |
| `KZ_SECS`, `KZ_RPARA`, `KZ_HEADSEC`, `KZ_NBLK` | `kernel.kz.json`, the packed kernel's shape | the kernel or its packing changes |

`BOOTHD_KSECS` and `BOOTHD_KOFS` are patched at install time from the file
actually written, so they are always right. `mbr.bin` carries nothing of the
kernel's. The source says it plainly at `boot/boothd.asm`'s pass 2: *"this
sector ships inside HDD.DRV, built from the kernel it will boot"* - true of
the BUILD, not of an install that crosses two builds.

**So it is not a knob problem.** Any in-place upgrade from a running system
whose kernel differs in one of those values writes a boot sector that cannot
boot what it installed. Same-build installs never see it, which is why no row
has: `instdeep`'s upgrade-in-place uses one build on both sides.

## 3. The owner's direction: the source's installer runs

Two shapes, and choosing between them is the owner's:

**A. The source's `HDDTOOL.DRV`.** Install, clicked on the running system,
loads `HDDTOOL.DRV` from the SOURCE volume instead of the system volume. Its
boot sector is then the source kernel's, by construction. What it runs into:
- `HD_ABI_VER` (`drivers/hdd/hddabi.inc`) refuses a tool of another vintage
  than the resident `HDD.DRV` - deliberately, for a half-copied floppy. An
  upgrade across an ABI bump would be refused. The install verbs would need
  a compatibility rule of their own, or the tool would need to carry what it
  uses of the transport.
- The tool runs on the OLD kernel's API. It may only use slots the old
  kernel has (the SDK-is-a-superset problem docs/UPSTREAM.md describes, from
  the other side), and must refuse by a version it can read before it
  writes anything.

**B. The source OS runs the install.** The machine is running the system it
is installing - booted off the source disk, as the workaround in 6 does by
hand - so every piece (kernel, `HDD.DRV`, `HDDTOOL.DRV`, boot sector) is one
build. For a machine that cannot boot the source directly (a staged online
download, a one-drive machine), that means a handoff into the new kernel
before the install: stage it, restart into it (the shape §87.5's hibernate
resume and KERN-DOS-PLAN's stub already use to replace a running kernel), and
let it run its own installer with a pending-install flag. **This is the shape
an online upgrade needs anyway**: a downloaded system has to be the thing
that commits itself, because nothing older can know its boot sector, its
drivers' ABI or its settings layout.

What B costs that A does not: the restart path, and a place to stage the new
system that survives it. What A costs that B does not: an ABI between two
vintages of `HDD.DRV`/`HDDTOOL.DRV`, permanently.

**The rule either way**: the boot sector, `KERNEL.SYS`, the drivers whose ABI
the kernel checks, and `SYSTEM.CFG`'s layout are ONE unit, and whatever writes
the boot sector must come from that unit.

## 4. What is NOT proposed

- A guard in today's installer (compare the source `KERNEL.SYS` against the
  boot sector `HDDTOOL.DRV` carries and refuse). It is cheap and would have
  turned this into a message, and the owner has declined it in favour of 3:
  the fix is the procedure, not a check on the wrong procedure. If 3 takes a
  while, it is still the smallest way to make the failure say its name.
- Patching `BLOB_SEG` & co. into the sector at install time like
  `BOOTHD_KSECS`. It would fix this one pairing and leave the general one
  (the drivers, the settings, the tool's ABI) exactly where it is.

## 5. The gate to write with it

A row that installs ACROSS builds: boot a volume installed from build X, put
build Y's system disk in B:, install keeping files, boot C:, require the
desktop. Y must move `BLOB_SEG` for the row to mean anything, and any knob
build already does: `DSK_OVLPAD` moves it 1 KB (measured above), and
`tools/os88build.py` builds it in a private tree, so the row needs no hand-made
kernel. Today it fails exactly as the field did; broken on purpose is today's
code. `tests/instdeep.py` is the harness to extend.

## 6. The workaround, until it is built

**Boot the disk you are installing from, and run Install there.** Its
`HDDTOOL.DRV` carries the boot sector for its own kernel. To go back, boot the
older disk and install from it the same way.

## 7. Where things are

- `drivers/hdd/inst.inc`: `hd_inst_vbr`, `hd_inst_keep`, the system phase
- `drivers/hdd/partw.inc`: `hd_bootvbr`, `hd_vbr_code`, `BOOTHD_KSECS`/`KOFS`
- `drivers/hdd/hdtool.inc`: loading the tool, and which volume it comes from
- `drivers/hdd/hddabi.inc`: `HD_ABI_VER`
- `boot/boothd.asm`: pass 2, the three `BLOB_SEG` sites
- `Makefile`: `BOOTHD_DEFS`, `KZDEF2`, `$(BUILD)/hddtool.bin`
- SPEC.md 52.10 (install), 52.10.15 (keeping files), 52.11 (the two images),
  2.9.13.5 (the hard disk's loader)
