# What a setting costs, and where the settings parser could live

**Status: BUILT.** SPEC.md §2.5.1 is the mechanism and §51.5.3 is the parser;
this is the record of how the design was picked and what it measured. Section 2
is the cost, sections 4–6 are the two rival designs, section 7 is why the free
one lost, and section 8 is what landed and what proved it.

The question that started it: *every time a setting is added it costs memory
and kernel space, and that seems to be mostly the parser — the parse only
happens at boot and in the Control Panel, so could the parser go in `.ovl`
(SPEC.md §2.5) and be duplicated into the panel's module (SPEC.md §2.8)?*

The premise was exactly right and it was worth **1,024 bytes of every
machine's RAM and twelve of the fourteen bytes a new setting costs** — both
measured on the shipped result, not predicted. Where the two designs differed
is *how* the overlay stays alive long enough to do the parse, and that turns
entirely on section 4 — a fact about the boot mount that SPEC.md §2.5 does not
state and that the obvious design does not survive.

Every number below was measured on `elendilon-new` at build 124
(`kernel-full.bin`, `KERN_BIG`), not estimated.

## 1 Where the parser is, and how big it is

`kernel/driver.inc`, SPEC.md §51.5. It is one contiguous block of `.cold`
between `drv_cfg_pack` and the end of `drv_cfg_save_x` — **577 bytes**, and
nothing else is interleaved with it:

| routine | bytes | boot | save |
|---|---:|:---:|:---:|
| `drv_cfg_load` | 48 | • | |
| `drv_cfg_deser` | 140 | • | |
| `drv_cfg_key` | 22 | • | |
| `drv_cfg_unpack` | 64 | • | |
| `drv_want_set` | 47 | • | |
| `drv_cfg_pack` | 67 | • | • |
| `drv_want_get` | 43 | • | • |
| `drv_cfg_ser` | 83 | | • |
| `drv_cfg_save_x` | 63 | | • |
| | **577** | 431 | 256 |

Its data is **93 bytes of `.text`** — `drv_cfg_keys` (45), `drv_cfg_map` (24),
`drv_cfgbit` (5), `drv_cfgname` (11), `drv_cfgsig` (8) — and **192 bytes of
`.bss`**: `drv_cfg`, the 72-byte live settings struct, and `drv_cfgbuf`, the
120-byte file buffer.

**The two callers are the whole call graph, and there are exactly two.**
`drv_boot_x` calls `drv_cfg_load`; `cp_flush_x` calls `drv_cfg_save` through
`COLD_SEG:drvf_drv_cfg_save`. Nothing else in the kernel, in a package or in a
driver names any of these ten symbols. The premise "only at boot and in the
Control Panel" is exactly right.

## 2 What a setting actually costs

Adding one plain one-byte setting today costs **14 resident bytes**, and
thirteen of them are the parser's rather than the setting's:

| | bytes | section | why |
|---|---:|---|---|
| a `drv_cfg_keys` row | 5 | `.text` | key, ver, len, struct offset |
| a `drv_cfg_map` row | 3 | `.text` | struct offset + the kernel variable's address |
| a `drv_cfg` byte | 1 | `.bss` | the staged struct |
| `CFG_FBUF` growth | 5 | `.bss` | `CFR_HDR` (4) + the payload; the buffer size is derived |
| the setting's own variable | 1 | `.bss` | **the only byte that is the feature's** |

`.text` and `.bss` are the two that share `KERN_CODE_MAX`, which has **1,991
bytes left** and is the guard docs/KERNEL-MEMORY.md says cannot be raised. So
the complaint is measured and correct, and the price is already visibly
shaping the design: SPEC.md §51.5's `TH` key packs the theme, the zoom and the
screen saver's three bytes into ONE record specifically to avoid "spending a
key entry and a record header (nine bytes of a rung with 49 in it)".

Note which way the ratio runs. The parser's *fixed* cost is 862 bytes and its
*marginal* cost is 13 a setting — so a tree that adds settings steadily pays
the marginal cost many times, and that is the number worth attacking.

## 3 What both designs do with it

Identical, and neither is the interesting part:

- **`.ovl` gets the boot half** — `drv_cfg_load`, `drv_cfg_deser`,
  `drv_cfg_key`, `drv_cfg_unpack`, `drv_want_set`, plus its own copies of
  `drv_cfg_pack` and `drv_want_get`. 431 bytes.
- **`CTRL.DRV`'s `.modc` gets the save half** — `drv_cfg_save_x`,
  `drv_cfg_ser` and its own `drv_cfg_pack` / `drv_want_get`. 256 bytes into a
  file, which costs disk and one sector of the panel's load, not RAM.
- **The tables and the file buffer go with the code, into both images.**
  `drv_cfg_keys`, `drv_cfg_map`, `drv_cfgbit`, `drv_cfgname`, `drv_cfgsig` (93
  bytes of `.text`) and `drv_cfgbuf` (120 bytes of `.bss`) are read only by
  these ten routines. CS-relative in both, which SPEC.md §2.5 and §2.8.6 both
  already allow and `tools/os88ovlchk.py` already exempts. `drv_cfg`, the
  72-byte struct, **stays** `.bss`: `osapi_drv_cfg` hands a driver its blob out
  of it at any moment in the session, so it is live state and not scaffolding.
- **Duplicated, but written once.** Both copies come from one `%macro` in a
  shared `kernel/cfgparse.inc`, instantiated in `.ovl` and in `.modc` with a
  label prefix. A new setting stays one edit to one table, which is the
  property SPEC.md §51.5 already claims for it and the one thing a hand-copied
  second table would destroy.

Five external calls change kind and four want a far entry that does not exist
yet — `drv_vol_bank`, `drv_vol_back`, `drv_mounted` and `ss_mins2idle`, 16
bytes of `.cold`. `dwf_dskw_read`, `dwf_dskw_stat`, `dwf_dskw_write_sys`,
`cw_thm_set`, `sched_mode_get` (its `cw_` shim went in size pass 2 — the body
is far-entered directly under §2.6.1) and `sched_mode_set` are all reachable
as they stand.

## 4 The problem both designs exist to solve

**`.ovl` is the FAT window, and the settings parse happens after the mount.**

SPEC.md §2.5's trick is that the overlay lands on `FAT_SEG`, whose bytes the
first mount is free to take — and every overlay entry today runs *before* that
mount. The settings parse cannot: `drv_cfg_load` reads `SYSTEM.CFG` **by
name**, so it needs a mounted volume, and `drv_boot_x` mounts one four
instructions earlier:

```
drv_boot_x:                     ; .cold
    call drv_mounted            ; -> dsk_chdir_q_x -> disk_mount_x
    jc .nodisk
    call drv_cfg_load           ; ...and this needs that mount
```

The parse cannot be moved earlier, because its own input is what the mount
produces. So either the overlay survives the mount, or it cannot host the
parse at all. Sections 5 and 6 are the two ways to make it survive.

## 5 Plan A — leave the overlay where it is and check whether it lived

Since SPEC.md §18.8.2 the mount often does not touch `FAT_SEG` at all.
`disk_mount_x` does three things in this order:

```
call dsk_fatw_want          ; claim a private FAT window out of the heap
call dsk_fatw_pick          ; [dsk_fatseg] = that claim, or FAT_SEG
call dsk_fat_window         ; read the FAT into [dsk_fatseg]
```

When the claim succeeds the nine sectors never reach `FAT_SEG` and the overlay
is intact. SPEC.md §2.5 records the symptom without drawing the conclusion:
"on a 640K machine the overlay is measurably still intact at the desktop".

So Plan A is: keep `.ovl` where it is, and have `drv_boot_x` ask the machine
whether it survived. `[dsk_fatseg]` is a live word and the FAT is the only
thing that ever writes that window, so the test is a fact rather than a
prediction:

```
    cmp word [dsk_fatseg], FAT_SEG
    je .load                    ; the FAT took it: defaults stand, which is
                                ; drv_cfg_load's existing CF=1 path
    call FAT_SEG:ovl_cfg_load
```

It costs nothing — no copy, no extra I/O, no heap — and §2.5's rule grows by
one sentence: *an overlay entry is dead once `[dsk_fatseg]` has been `FAT_SEG`
at a mount.* Before the first mount that is never true, so every existing
entry is unaffected.

### 5.1 And it does not work on an installed machine

**`dsk_fatw_want` refuses the boot volume outright when the machine boots from
its hard disk**, so `[dsk_fatseg]` is `FAT_SEG` at that mount every time,
regardless of how much heap is free:

```
    mov bl, [disk_drive]
    cmp bl, 2                   ; volumes 0 and 1 are the BIOS floppies; a
    jae .out                    ; driver volume claims at osapi_vol_add
```

A boot partition is neither. `dsk_boot_from_x` adopts it as a **`DVK_BIOS` row
at index 2** — always C: (SPEC.md §18.7.1) — reached over int 13h with the
geometry from `AH=08h`, and that adoption is "what lets the kernel read
SYSTEM.CFG and load HDD.DRV off the volume that driver would otherwise have
been needed to reach". So no driver is involved at boot, and consequently
`osapi_vol_add` never runs for it either: the row that most needs a private
window is the one row that can never be given one.

The fix is available — `dsk_fatw_slot` allows any `BL < DVOL_MAX` (8), so
`drv_boot_x` can call `dsk_fatw_claim` for `[dsk_bootvol]` itself before
`drv_mounted`, and `dsk_fatw_want` then finds a claim already there and
returns. But that is a policy change in the mount path, and it leaves Plan A
resting on a 4,608-byte heap claim succeeding at boot on **every** machine,
with "the settings silently do not load" as the failure. Plan A's whole appeal
was that it cost nothing; a claim it cannot do without is not that.

Two further limits, true even with the claim:

- **The overlay is capped at 1,780 more bytes.** `OVL_SIZE` is 2,828 of
  `FAT_PARA * 16` = 4,608, and that ceiling is the FAT's, not a budget.
- **It buys the parse and nothing after it.** The overlay is still forfeit the
  moment anything re-windows the FAT, so `drv_boot_x` (147 bytes) and
  `drv_notice_x` (23) — boot-only code, resident forever — stay where they are.

## 6 Plan B — move the overlay above `.lowbss`, into what becomes the heap

Put `.ovl` at the heap instead of at the FAT window. Then no mount can reach
it on any machine, the cap becomes the heap rather than 4,608 bytes, and the
overlay stays alive through `drv_boot` — so the whole boot-only class can move,
not just the parse. It has to be **released before the heap is really used**,
or the region it sat in is a hole somebody else's claim has to work around.

Three ways to get it there, and the cheapest is the one that changes least:

| | how | costs |
|---|---|---|
| **B1** | let the boot read carry it: `.ovl` at `HEAP_SEG`, with the FAT and `.lowbss` gaps emitted as zeros | **+15,872 bytes of KERNEL.SYS** — 31 sectors of boot read at the measured 7,980 ms / 208 sectors ≈ **+1,190 ms on every boot**, and 16 of the 360KB system disk's 30 free clusters. Refused |
| **B2** | a second `int 13h` in the boot sector placing `.ovl`'s sectors at `HEAP_SEG` | +1 call ≈ 400 ms, and the growth lands in a 512-byte sector that is already tight |
| **B3** | the boot read puts it at `FAT_SEG` exactly as today, and `kmain` **copies** it up after `mem_init` | 1,414 `movsw` ≈ **7.4 ms**, ~30 bytes of resident code, no disk change, no boot-sector change |

**B3, into a `mem_claim_hi` block.** That is where the kernel already puts
code images — `loader.inc`'s comment is "from the TOP down, away from the data
claims", and `drv_load` and `mod_need` both use it because "a driver's base IS
its CS, so it can never move". The relocated overlay is exactly that kind of
object, and putting it there is what makes the release clean rather than
merely survivable: **claimed high, freed last, nothing above it, no hole.** A
block at the bottom of the heap freed at the end of `drv_boot` would sit under
the FAT window, the read-ahead cache and every driver image claimed while it
lived — reusable, but a hole.

Two mechanics fall out:

- **Three call sites stay immediate and four become indirect.** `kmain` calls
  the overlay seven times; `ovl_cpu_detect`, `ovl_xm_sniff` and `ovl_clk_init`
  run before `mem_init` (MARK 11) and keep `call FAT_SEG:`. The copy happens
  at MARK 12, and `ovl_font_init`, `ovl_desk_init`, `drv_snd_sniff`,
  `ovl_snd_init` and everything new go through a far pointer. ~24 bytes of
  `.text`, or one shim taking the offset in AX.
- **The drop is `mem_free` at MARK 29**, the end of `drv_boot` and the last
  moment anything boot-only can want. `xm_boot` (MARK 30) loads `XMEM.DRV`
  into a high claim, so it is the first thing to reuse the space.

`mem_claim_hi` at MARK 12 asks for 3KB of a completely empty heap, so the
refusal path is unreachable on any machine that boots at all — but it is still
a refusal, and it takes Plan A's fallback: leave the overlay at `FAT_SEG`,
skip the late entries, defaults stand.

**What Plan B owes `tools/os88sym.py`.** It maps `.ovl` → `FAT_SEG`
statically, and under B3 an overlay symbol's segment is only known at run
time. That file's own header calls a plausible wrong segment "the exact
failure this file was written to stop", and it has cost a session before — so
the tool has to read the far pointer out of the guest, or refuse to answer,
rather than keep printing `FAT_SEG:`. B1 and B2 keep a compile-time constant
and do not owe this.

## 7 The comparison, and what I would build

| | Plan A | Plan B (B3) |
|---|---|---|
| overlay survives a **floppy** boot mount | with a 4,608-byte heap claim | always |
| overlay survives a **hard-disk** boot mount | **no** — §5.1, and only with a new claim in the mount path | always |
| depends on the heap | 4,608 bytes at the mount, or the settings are lost | 3KB at MARK 12, on an empty heap |
| room for more | 1,780 bytes, and the FAT sets it | the heap |
| alive until | the first mount | the end of `drv_boot` |
| can also move `drv_boot_x` + `drv_notice_x` (170 B) | no | yes |
| boot cost | none | **7.4 ms** |
| fragmentation | none | none, claimed high and freed last |
| `os88sym.py` | unchanged | owes it a change |

**Plan B, B3.** It is unconditional where Plan A is conditional on the one
configuration that matters most, it is bounded by the heap rather than by a
FAT, and 7.4 ms of `movsw` against a 9.2 s boot is not a trade so much as a
rounding error. Plan A's only advantage was costing nothing, and §5.1 takes
that away.

### 7.1 What it was worth, measured on the result

Two commits, each gated on its own. Against the tree immediately before them,
`KERN_BIG`:

| | before | after | |
|---|---:|---:|---|
| `.cold` | 39,185 | 38,593 | −592 |
| `.text` + `.bss` | 63,601 | 63,399 | −202 |
| image rung | 64,000 | **63,488** | −512 |
| cold rung | 39,424 | **38,912** | −512 |
| `KERN_SIZE` | 119,296 | **118,272** | **−1,024 on every machine** |
| `KERN_CODE_MAX` headroom | 1,935 | 2,137 | +202 |
| `.ovl` | 2,828 | 3,600 of 4,608 | the mechanism's 108 plus the parser's 664 |
| `CTRL.DRV` | 6,424 (13 sec) | 6,900 (14 sec) | one more sector in one existing read |
| the ladder | HEAP at 118.0 KB | **117.0 KB** | the kernel's whole span, on every machine |

And the marginal cost of a new one-byte setting, which is what was asked:

| | before | after |
|---|---:|---:|
| a `_keys` row | 5 `.text` | 0 |
| a `_map` row | 3 `.text` | 0 |
| a `drv_cfg` byte | 1 `.bss` | 1 `.bss` |
| `CFG_FBUF` growth | 5 `.bss` | 0 |
| the setting's own variable | 1 `.bss` | 1 `.bss` |
| | **14** | **2** |

`.ovl` has ~738 bytes left, which at 13 a setting is about fifty-six more
before the BLOB binds rather than `KERN_CODE_MAX`. It used to be the FAT
window; since SPEC.md §2.9.6 the overlay rides stage 2's rung and the ceiling
is `BOOT2_PAD - OVL_AT`, which is deliberately the same 4,608 bytes.

### 7.2 The standing cost, and it is accepted

**A machine with no room for the overlay does not boot.** `ovl_relocate` used
to print `RAM: no heap for the boot overlay` and halt, with deliberately no
fallback, because everything the overlay does is load-bearing — the typeface,
the desktop's volume zones, the sound layer and the settings file. That
refusal went with the claim (SPEC.md §2.9.6): the overlay is part of stage 2's
blob, and a machine that cannot hold the blob stops at the boot sector's own
`RAM` halt instead. Same answer, earlier, from the one place that can give it.
SPEC.md §2.5.1.1 is the argument in full.

What follows from it is the thing to keep in mind when using this mechanism
again: **the kernel's minimum RAM now includes the blob's rung, and it moves
when the overlay does.** Anything moved into the overlay to save resident bytes
raises that floor by what it saved. Guard 5 in `kernel.asm` is that as an
assertion, and since the overlay joined the blob it is the *binding* guard,
ahead of `KERN_BUDGET`: 3,072 bytes of MIN_RAM headroom against the budget's
5,120. The trade is accepted and is written down rather than discovered.

## 8 What landed, and what proved it

Two commits, because they are two mechanisms and the first buys nothing on its
own:

1. **SPEC.md §2.5.1** — the overlay outlives the first mount. It did that first
   by relocating: `ovl_relocate` claimed `OVL_KB` with `mem_claim_hi` at MARK
   12, copied the overlay into it and pointed `[ovl_fseg]` at the new home,
   with three call sites left immediate and the rest through `OVLCALL`. Since
   SPEC.md §2.9.6 it does it by living in stage 2's blob instead — no claim, no
   copy, one call kind, and the `mem_free` gone with them.
2. **SPEC.md §51.5.3** — the parser as four macros, expanded once into `.ovl`
   with the prefix `ovc` and once into `CTRL.DRV`'s `.modc` with `cpc`, tables
   and file buffer included. `drv_cfg`, `drv_cfgname` and `drv_sysname` stay
   resident and the section says why.

**Verified on a cycle-accurate 4.77 MHz 8088, not inferred.** `tests/cfgtrip.py`
(registered `soak`) is the assertion the build cannot make: the two halves no
longer share a segment, a table or a buffer, and every way of getting that
wrong assembles cleanly and still boots. So it drives the round trip — poke
three settings, close the Control Panel, and boot **the disk the guest wrote**:

```
boot 1, defaults: {'clk_h12': 0, 'ss_modes': 15, 'ss_mins': 5}
boot 1, after the close: cp_dsave = 0 (0 = written)
boot 1, the disk: 4 sector(s) changed, added ['SYSTEM.CFG']
boot 1, SYSTEM.CFG: 120 bytes, head b'O88CFG\x00\x00'
boot 2, read back: {'clk_h12': 1, 'ss_modes': 2, 'ss_mins': 7}
boot 2, ovl_fseg = 0FE0 (the overlay is dropped by here)
```

Two things about it are worth keeping:

- **MartyPC never writes a mounted floppy back**, so the first version of this
  test rebooted the *pristine* image and reported three failures that read
  exactly like a broken writer. `tools/os88flush.py` is what actually gets the
  guest's disk onto the host (docs/MARTYPC-DEBUG.md), and the file's bytes are
  read by a FAT12 walker that shares no code with the kernel — so "os8088 wrote
  a file" and "there is a file" stay separate claims.
- **The change is a poke, not a click.** What is under test is the parser;
  `cpc_pack` walks `_map` and copies the live kernel bytes, which is exactly
  what a click leaves behind, and `tests/dispcp.py` already drives the widgets.
  No layout constant in this file can go stale. `[clk_secs]` is deliberately
  *not* one of the three settings: a menu bar showing seconds redraws once a
  second, so `settle` never sees a still screen.

## 9 One thing found on the way, fixed separately
**Eleven far entries nothing called, 44 bytes of `.cold`** — landed on
`elendilon-new` on its own, because it has nothing to do with the settings.
A far entry does not know who far-called it, so `dvf_*` and `drvf_*` in
`kernel/driver.inc` are the same four bytes twice; the `drvf_` set was written
when the Control Panel became a module (SPEC.md §2.8) and its callers became
far, and the `dvf_` half those callers had just stopped using was never
deleted. Nine were exact duplicates and two — `dvf_drv_blk_call`,
`dvf_drv_cls_fp` — were dead outright, their bodies near-called from inside the
cold segment. SPEC.md §2.6.1 names this defect twice already without the set
having been swept, and `tools/os88ovlchk.py` cannot see it: an unreferenced
label is not a wrong call.

**And one thing not worth doing on its own.** `drv_cfg_keys` carries a byte it
can derive: SPEC.md §51.5 states the keys tile `drv_cfg` exactly and they are
declared in struct order, so each row's offset is the running sum of the `len`
fields before it — 9 bytes of `.text` today and 1 a setting. It is not free
(`drv_cfg_key` would have to return the offset it accumulated, and the
invariant becomes load-bearing rather than descriptive), and if section 3 lands
it is 1 byte of a section that costs nothing. Worth doing only if it does not.
