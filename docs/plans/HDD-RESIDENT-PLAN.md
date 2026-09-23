# What `HDD.DRV` still costs a machine that has a hard disk

**OPEN, and deliberately NOT STARTED.** Everything measured below is true of
the tree at `d1ceac13`. The work that SHIPPED is SPEC.md 22.6, 52.13, 52.13.3
and 52.13.4 and is not repeated here — this file is what is LEFT, why the
obvious next step buys nothing, and the one gate that should exist before any
of it is taken.

## 1. Where it stands, and the number that matters

| | image | what `drv_load` CLAIMS |
|---|---|---|
| start of the day | 8,664 | 9 KB, plus 24 KB of listing claims |
| after SPEC.md 22.6 | 8,152 | 8 KB |
| after 52.13 (the page moved out) | 5,633 | 6 KB |
| after 52.13.3 (the buffer union) | 5,121 | 6 KB |
| after 52.13.4 (one byte moved) | **5,120** | **5 KB** |

**THE BAR IS 1,025 BYTES AND NOTHING ELSE IS A BAR.** `drv_load` rounds an
image up to whole KB and claims that, so the machine pays 5 KB for anything
from 4,097 to 5,120. A change that saves 400 bytes saves the user **nothing**.
The next kilobyte needs the image **under 4,096**.

This is not the rung heuristic CLAUDE.md's banner refuses. `KERN_BUDGET`'s
512-byte steps are a REPORTING granularity over bytes that are all resident
either way; `drv_load`'s KB is **the allocation itself**. The byte rule still
governs which changes are worth making — it is this threshold that decides
whether a given change is worth making *now* or is 400 bytes banked for
whoever crosses 4,096.

## 2. What is left, by measured bytes

Symbol spans off the map, `HDD.DRV` at 5,120:

| block | bytes | mount-only? |
|---|---|---|
| `hdd.asm` less the IDE rung | 1,132 | partly — `hd_probe` is |
| the IDE rung — 8 procs (`hd_idbuf` is now unioned) | **566** | yes, entirely |
| `hd_mbr` / `hd_idbuf`, the shared 512-byte sector buffer | **512** | yes |
| `hdtool.inc` — the loader and the five page thunks | 885 | no |
| `hdcom.inc` — partition-table reading, shared with the tool | 598 | mostly |
| `cfg.inc` — `SYSTEM.CFG` geometry | **501** | yes |
| `mount.inc` — mount/unmount | **607** | yes |
| `hdsvc.inc` + misc | ~320 | no |

The mount-only column is **~2.2 KB**, comfortably over the 1,025 bar. No
subset of it is.

## 3. Three refusals, so they are not re-derived

**A second `HDDTOOL.DRV` load at boot is REFUSED — the owner's call.** The
shape is obvious: the resident pulls the tool in at `DRVV_READY`, probes,
automounts and drops it. It costs a ~11 KB compressed read on every boot of
every hard-disk machine — a second or two on a 5150 — to save one kilobyte of
RAM. That is a poor trade on the machine this project is calibrated against.
If the mount-only work is taken it wants **a flow of its own**, not the page's.

**The sector buffer as a heap claim, ON ITS OWN, is REFUSED.** It was
authorised and it is still the wrong change alone: it is worth ~400 net bytes
(512 out, ~66 back for a static partition table the near-indexing callers
need, plus the claim/free code), **400 bytes moves no kilobyte**, and it adds
a mount that can fail for want of heap. It becomes free the day the code that
uses it leaves, because `hd_mbr` and `hd_idbuf` both belong to the mount and
probe paths. Take it WITH them or not at all.

**`hd_part_ent` is why the claim is not a one-liner.** It returns a NEAR
pointer into `hd_mbr` and its callers index `[si+HP_TYPE]` through DS. In the
tool that is right and must stay — the tool EDITS and WRITES the sector. Only
the resident's copy can move, and `hdcom.inc` is one source compiled into both
images, so the divergence has to be designed rather than patched in.

## 4. The constraint, stated by the owner

A mount must keep working:

  * when the driver is **loaded** (`DRVV_READY`'s automount);
  * from the **Control Panel** — which is free, because SPEC.md 52.13 put the
    page in `HDDTOOL.DRV`, so the image is already in memory whenever Mount is
    clickable;
  * and after a **hibernate reload** — `hbm_detach`/`hbm_reload` unload and
    remount drivers around a hibernate, so this is `DRVV_READY` again.

A buffer that is claimed only while loading, and whose failure fails the
mount, is acceptable.

## 5. `hd_probe` is the awkward one

It runs at `DRVV_ATTACH` and its `CF=1` is what makes the driver **refuse to
attach**. Moving it wholesale means loading a second image to discover there
is no disk. The shape that survives is a minimal resident probe — *does
anything answer `int 13h AH=08h`* — with geometry detection and the whole IDE
rung on the far side.

## 6. THE GATE THAT DOES NOT EXIST, and it is the first thing to build

**No QEMU hard-disk test exists.** Every `hd*` row in `tests/suite.py` —
`hdboot`, `hddcp`, `hdnoclaim`, `knobhd`, `kdhdd` — runs under MartyPC, which
is an 8088 behind an option ROM and therefore takes **rung 0** every time. So
**SPEC.md 52.1's IDE rung has never been exercised by anything in this tree**,
and that was true long before today's work.

It is on CLAUDE.md's short list of QEMU's seven legitimate cases for exactly
this reason (case 2). QEMU's CPU is 486-class, so `hd_probe`'s
`OSAPI_CPU_INFO` / `cmp al, CPU_286` passes and rung 1 runs whenever there is
an IDE controller — **even when SeaBIOS already knows the drive**, because
`hd_ide_dup` has to issue IDENTIFY before it can decide the row is a
duplicate. So the rung is reachable with an ordinary `-drive if=ide`.

**The gate wants `ethertest`'s shape and no UI driving at all**: a 360 KB
system disk whose `SYSTEM.CFG` already sets the hard disk's `drv_cfgbit`, so
the driver attaches and automounts before the first paint and the row READS
STATE rather than clicking. `tools/heapmap.py` already carries a `pmemsave`
guest-memory reader to copy.

What it should assert:

  1. `[hd_dupd]` is non-zero, or a row is `HDK_IDE` — either proves
     `hd_ide_ident` ran, which is the only thing that writes `hd_idbuf`;
  2. a volume mounted and its root lists — which proves `hd_mbr` held a real
     partition table AFTER the probe had used the same 512 bytes, and is the
     live assertion behind 52.13.3's lifetime argument;
  3. the geometry `IDENTIFY` reported is sane.

`tools/os88hdd.py` builds a partitioned FAT16 image already (`tests/kdhdd.py`
is the caller), so the disk half needs no new tool.

**The owner has hand-tested the IDE path working at `d1ceac13`**, so this is a
gap in the SUITE rather than a suspected defect — which is why it is deferred
rather than urgent. It should still land before any change that moves the
probe.

## 7. Order, if it is taken

1. The QEMU IDE gate (6). Nothing below should be attempted without it.
2. A flow for the mount-only code that is not the page's load (3, 5).
3. `hd_mbr`/`hd_idbuf` travel with it (3), and the partition-table reader's
   two-image divergence is designed at the same time.

Under 4,096 the machine pays **4 KB**. Above it, it pays 5 KB whatever else
is done.
