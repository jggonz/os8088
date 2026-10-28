# Movable `.cold`, and the VGA code a mono machine gives back

> **BUILT, all three waves, on branch `movable-cold`** - SPEC.md 2.10.9 is
> the contract and this file is the design record. Everything below the
> line was written BEFORE any of it was built, at `2bc3442` (build 339), and
> is kept as written; where the build came out differently, this box says
> so. Measured at build 474:
>
> * **A machine with neither a VGA nor an EGA keeps 95,232 bytes resident
>   against 97,280 on a VGA or EGA machine - 2,048 back.** The kernel before
>   this work was 97,792 on VGA and 97,280 on mono (which already declined
>   `.vgabuf`), so VGA/EGA machines gain 512 (wave 2) and mono 2,048.
> * **The rung is 2,032 bytes, not the ~3.3KB §3 estimated.** §3's figure
>   counted every VGA-only byte; ~1.1KB of them are short arms inside
>   `font_char`, `gfx_points` and `gfx_blit4`, where a `jmp` would land on
>   every call of the hottest paths there are, and they stay in `.text`.
>   So do `vga_gc_reset` and `gfx_1bpp_on`/`off` (called on every adapter)
>   and the VESA rows (tables walked on every machine).
> * **Wave 1 went differently in three places.** The module lists did not
>   move into the module files: `mod_cfix` SCANS a loaded module for its far
>   calls into `.cold`, and `tools/os88rom.py` proves on every build that the
>   scan finds exactly the two-assembly diff's words - no table anywhere
>   (`COLDHMA.SYS` 3,210 -> 2,763 bytes). The kernel's own list did not fit
>   the blob - its 14th sector costs 360KB and 720KB an `int 13h` call each -
>   so it rides in `.cfx`, a section after `.cold` in the file that lands
>   under the blob's lift, and the slide's code rides with it. And
>   **kern_small keeps the ROM's module lists and `ROMF_FIX`**: it has no
>   slide, and SPEC.md 39.27.4 spends nothing there, so its `.text` and
>   `.cold` are the base's plus §5's floor round-up (five bytes) and the
>   parity pad that keeps `dsk_xfer` and its neighbours even behind it.
> * **§5's packed top is -512 on both kernels at this build**, as predicted
>   for kern_big's VGA machines; kern_small gets it too.
> * **The window**: 6,432 bytes of KERNEL_SEG's 64KB left above the rung,
>   which `kernsize` prints as `window` - `KERN_BUDGET` (3,584 spare) still
>   binds first.
> * **Gates**: `tests/vgarefs.txt` (os88ovlchk rule 2f), os88cfx refusing a
>   `.cold` word in the rung, the window guard, `cfix_prove`, and the soak
>   row `coldslide`. `tools/os88marty.py`'s `sym()` follows a slid `.cold`.
> * **The HMA on a slid machine needed a second hash**, which §4 did not see:
>   `COLDHMA.SYS`'s `.text` hash covers the very words the slide rewrites, so
>   it refused every slid kernel. It carries both now and picks by
>   `[api_coldseg]` (SPEC.md 2.10.9) - skipping the words instead would be a
>   span per site, ~370 bytes the kern_big ROM has not got. A 286 with a CGA
>   then gets the rung AND both copies of `.cold` back: floor 0EA0 against
>   1820. `tests/hmacold.py` leg D is that machine (a `VIDEO=cga` tree).
>   The final soak pass found it, together with a register the first cut of
>   the delta mover clobbered (DX loaded before the hash that reuses it), which
>   had every ROM and HMA adoption refusing.

It answers docs/plans/MONO-RECLAIM-PLAN.md's open question, *"the RAM that
is mono-only has no customer"*, the other way round: give it back rather
than find something to put in it. That plan's §3 refused the same idea once,
because a rung of its own made every call to the VGA code **far**. §2 below
is why that refusal does not hold any more. §5 is the second question the
owner asked before starting: does `.cold` need its 512-byte rung at all?
(No.)

## 0. The verdict, up front

1. **Feasible, and with no cost in cycles.** On this tree `.cold` starts
   **59,904 bytes** above `KERNEL_SEG`, so the 64KB window `CS = KERNEL_SEG`
   reaches has **5,632 bytes** left above the top of LOW. A VGA rung placed
   there, between LOW and COLD, is still reached by NEAR calls, and its data
   is still DS-near. On a machine with no VGA and no EGA, `.cold` slides down
   over it at boot.
2. **What it is worth: about 3.3KB of heap on a CGA, Hercules or MDA
   machine** (§3). A VGA or EGA machine pays the mechanism, a few dozen
   resident bytes, plus whatever the rounding does.
3. **Moving `.cold` needs no new bytes in `.cold`.** It is already free to
   move by segment (§1). What makes a move hard is that the words naming it
   are listed by the DESTINATION (the ROM, `COLDHMA.SYS`) and checked against
   a FIXED home value. Wave 1 (§4) makes every mover a delta mover and makes
   each file carry its own list. That is worth doing even if the VGA rung
   never ships.
4. **`.cold` does not need 512-byte alignment** (§5). Guard 6 names the
   bases an `int 13h` transfer lands on, and `COLD_RAM` is not one of them.
   Its rung only keeps `HEAP_SEG` aligned, and one round-up at the heap floor
   does that for everything between the top of LOW and the heap.

## 1. What we actually do today

**`.cold` itself is never patched.** SPEC.md 2.10.2 made it position-
independent across segments: no byte inside it names its own segment,
cold-to-cold far calls are `COLDCALL`, a far pointer back into it takes its
segment from `COLDSEG_TO`, and `tools/os88romfix.py --check` (soak row
`coldpic`) fails any word inside `.cold` that moves with its segment. Its
OFFSETS are fixed (`vstart=COLD_VSTART`). Its SEGMENT is free. So a move is:
copy `.cold` verbatim, then rewrite every word OUTSIDE it that names its
segment.

| where `.cold` goes | who moves it | where the word lists live | contract |
|---|---|---|---|
| ROM, F400 | `rom_patch` in the ROM's tail. The boot overlay's doorbell (`kmain_o`, straight after `dsk_boot_from_x`) far-calls it | the ROM's tail | SPEC.md 2.10.4 |
| HMA, FFFF:`HMA_COLD_AT` | `COLDHMA.SYS` (2,037 bytes, system disk root). `XMEM.DRV` reads it at attach and far-calls it; 1,109 bytes stay resident in the HMA | inside `COLDHMA.SYS` | SPEC.md 2.10.7 |
| above 1MB, kern_mega | `mega_coldup` re-bases a GDT descriptor | (a selector, not a list) | KERN-MEGA-SEGMENTS-PLAN |

Both list-carrying movers refuse unless (1) `.cold` matches byte for byte
(ROM) or copies and reads back (HMA), (2) a hash over `.text`'s code
matches, and (3) **every listed word still says the BUILT value**
(`COLD_RAM`/`COLD_SEG`). Then they rewrite the words, re-point
`[api_coldseg]` and `[rom_vgabw]`, and install `[rom_mfp]`.

What a move has to rewrite on this tree (`tools/os88rom.py hma --build
build`, build 339):

| population | words | when |
|---|---:|---|
| `.text` | 93 | at the move |
| the boot blob (`.boot2`) | 41 | at the move (it is still alive then) |
| `.vgabuf`'s one segment load | 1 | at the move |
| the FAT-window half (`.ovlw`) | ROM only | at the move |
| `[api_coldseg]`, the run-time `mod_fp` slots | n/a | at the move (HMA rewrites every slot that names the old segment) |
| **10 on-demand modules** | **182** | at EVERY load: `mod_need` far-calls `[rom_mfp]` with `ROMF_FIX`, a door only the ROM or the HMA image supplies (`rom_none` passes it and does nothing) |

`tools/os88rom.py` derives every list by assembling the kernel twice with
`.cold`'s segment moved and diffing the two.

**Three things make another mover expensive, and none of them is in
`.cold`:**
1. Each mover checks the words against a fixed home value, so movers cannot
   follow one another. A `.cold` that has moved once cannot be moved again.
2. The lists ride with the destination, so a new mover means another copy of
   them.
3. A moved kernel's modules need a RESIDENT fixer plus 182 words of tables.
   The ROM and the HMA can afford that because their copies sit outside
   conventional RAM. An in-RAM move could not.

**Refused: deleting the fixups instead.** Routing every reference through
`[api_coldseg]` (the `api_far` shape) would cost about 3 bytes at each of
the 93 `.text` sites, plus cycles on hot paths that already go through
`.cold`, `mem_claim` among them. A list applied once by transient code costs
no resident bytes and no cycles.

## 2. The VGA rung, and why near calls survive it

The ladder on this tree (`kernsize`):

```
KERNEL 0x0060  FAT 0x0c60  LOW 0x0d80  COLD 0x0f00  VGABUF 0x1840  HEAP 0x1860
```

`(0x0F00 - 0x0060) * 16 = 59,904`. The window `KERNEL_SEG` reaches ends at
65,536. MONO-RECLAIM-PLAN section 3 assumed a rung of its own was outside that
window. A rung placed right above LOW is inside it.

**Proposed ladder:**

```
[image .text+.bss][FAT][LOW][VGA][COLD][VGABUF] heap
```

- **`.vga` is a section** with `vstart = (VGA_RAM - KERNEL_SEG) * 16`, so
  its labels are `KERNEL_SEG` offsets and `.text` calls it with plain
  `call`. CS = DS = `KERNEL_SEG` inside it, exactly as in `.text`.
- **In the file** it is the section right before `.cold`, after the zero gap
  the FAT window and `.lowbss` leave (LZ4 packs that gap away), so stage 2's
  one contiguous read lands it with no loader change.
- **Its data**: VGA-only `.bss` (57 bytes, 96 with VESA) goes in a nobits
  tail of the rung, DS-near like the rest. `.vgabuf` stays where it is,
  already dropped on a machine with no VGA (`mem_floor_ax`).
- **It holds** the VGA-only bodies from vga12.inc (MONO-RECLAIM-PLAN section 1.1's
  table), the planar decoder, and the VESA hook (`vidext.inc`, a VGA
  superset).
- **What stays in `.text`** is every DOOR, meaning the `[vid_mono]` /
  `[vid_planes]` test that decides between renderers, and every body BOTH
  renderers share (`vga_rect_setup`, `vga_xor_fill_vram`'s entry; MONO-RECLAIM-PLAN
  section 1.1 found two of those by building, not by name).

**The one cycle it costs.** `gfx_fill_raw` FALLS THROUGH into
`vga_solid_rect` (MONO-RECLAIM-PLAN section 1.2). With the body in the rung, the
VGA arm becomes one near `jmp`: about 15 clocks (~3 us) per fill, on VGA
machines only, against a fill's ~756 us fixed part. The mono arm is
byte-identical. Every other entry is already a `call` or a `jmp`.

**The decision**: drop the rung when `[vid_avail] & (VID_A_VGA | VID_A_EGA)`
is clear. Not `[vid_mono]`: a VGA machine can switch displays at run time
(SPEC.md 39.11), and EGA is planar and runs on these bodies. A machine
without either card can never gain one, so the decision is made once, at
boot.

**The slide** (machines with neither card, and with no ROM):
1. In `kmain_o`, after the ROM doorbell and after video detection, test
   `[api_coldseg] == COLD_HOME` (the ROM did not adopt) and the card bits
   above.
2. Copy `.cold` down by `VGA_PARA` paragraphs (`rep movsw`, overlapping
   downward, so DF = 0 is correct). 37,767 bytes at 13.3 cycles a byte is
   ~500K cycles, **~105 ms on a 4.77 MHz 8088** (ESTIMATE, PERFORMANCE.md
   Set 117.2's rate). The packed kernel's decode is 799 ms by comparison.
3. Apply wave 1's list (§4) with delta `-VGA_PARA`. That covers `.text`,
   the blob, `[api_coldseg]` and the `mod_fp` slots. `mod_init_x` fills
   those slots from a listed blob word, so a slide before it needs nothing
   extra.
4. The heap floor follows from `mem_floor_ax` (§4, wave 2).

**A ROM machine needs no slide.** When the ROM adopts `.cold`, the floor
already falls onto `COLD_RAM`. It falls onto `VGA_RAM` instead, past both
dead rungs. That includes the owner's 5150.

**An HMA machine** (a 286 or better, which almost always means VGA) works if
the HMA install is a delta mover (wave 1), because on a 286 with CGA or
Hercules the slide may already have happened.

**The gate the plan cannot ship without.** Once the rung is dropped, a call
into it on a mono machine runs `.cold`'s bytes. MONO-RECLAIM-PLAN section 3 already
names the tool: a `tests/vgarefs.txt` in the shape of `tools/os88ovlchk.py`
and `tests/ovlrefs.txt`. It lists every reference into `.vga` from outside
it and says which `.text` door guards each one. A new caller then fails the
build until somebody argues it in.

## 3. What it is worth

**Measured**, by assembling a scratch `kern_big` with `GFX_VGA`,
`GFX_PLANE` and `VID_EXT` off. It needed two one-line stubs to assemble
(`wm_su_arm`'s `call gfx_1bpp_on` and one `vid_planes_w` multiply in the
`STIKMENU.DRV` module), and it was NOT booted, so treat it as a ceiling:

| | `.text` | `.bss` | `.cold` | `.lowbss` | `.vgabuf` |
|---|---:|---:|---:|---:|---:|
| VGA, beyond VESA | 3,155 | 57 | 288 | 14 | 336 |
| the VESA hook (`NOVIDEXT`) | 330 | 39 | 26 | 0 | 0 |

The `.cold` share stays where it is: it runs with CS = `COLD_SEG` and cannot
move into a near rung. The inline dispatch stays too: MONO-RECLAIM-PLAN section 2.2
estimated ~220 bytes of it on `kern_small`. **So the rung is ~3.2-3.4KB.**
`.vgabuf` is already given back today, so it is not part of the gain.

On today's rung ladder, with the rung ESTIMATED at 3,330 bytes (a 3,584-byte
rung) and `.text+.bss` falling 48,887 to ~45,561 (image rung 49,152 to
46,080):

| | today | rung ladder | §5's packed top |
|---|---:|---:|---:|
| `KERN_SIZE`, VGA or EGA machine | 98,304 | 98,816 | 98,304 |
| what a CGA/Herc/MDA machine keeps resident | 97,792 | **94,720** | **94,720** |

So: **~3KB back on a machine with neither card**, measured in rungs, and the
3.3KB the rung holds, measured in bytes. A VGA machine's rung-ladder +512 is
two partly filled rungs where there was one. Its BYTE cost is the mechanism,
estimated at 30-60 resident bytes (`mem_floor_ax`, `osapi_sys_kb`'s third
state, `mod_need`'s loop, the `jmp`). §5's packed top removes the rounding.

**What it spends that is not RAM:**
- **Window headroom.** The constraint becomes `.text+.bss + FAT + LOW + VGA
  <= 65,536`. That leaves ~5.1KB for `.text` to grow, where
  `KERN_CODE_MAX` leaves 16,649 today. It does not bind:
  `KERN_BUDGET` has 2,560 spare, so the budget runs out first. A new guard
  states it (§6).
- **~105 ms of boot** on a mono machine with no ROM. Nothing on any other
  machine.
- **Blob room** for the mover and its list, perhaps one sector (~24 ms in
  the run, no extra `int 13h`, MONO-RECLAIM-PLAN section 3).

## 4. Waves

**Wave 1 - every mover becomes a delta mover, and each file carries its own
list.** No ladder change. Worth doing even if wave 3 never ships.
- **A list in `KERNEL.SYS`.** The `.text` and blob words, delta-coded as
  byte gaps (~140 bytes ESTIMATED for 134 words), in the transient blob, plus
  `cold_move` in `.ovl`: given a new segment, check every word equals
  `[api_coldseg]`, copy `.cold` from where it IS, add `new - old`. No
  resident byte. `tools/os88rom.py`'s double assembly already produces the
  list. It moves into the kernel build, and `os88romfix.py --check` keeps
  gating it.
- **A list in each module file.** The 182 words, as a relocation trailer in
  each `.DRV` module, read with the image and applied by a ~25-byte loop in
  `mod_need`: delta = `[api_coldseg] - COLD_HOME`, skipped when zero. Same
  shape as SPEC.md 2.10.8's driver relocation, which `tools/os88rom.py`
  already proves by applying it and comparing.
- **The ROM and `COLDHMA.SYS` lose their module tables** and `ROMF_FIX`. The
  ROM gets the space back for modules (SPEC.md 2.10.5's greedy fill), and
  `COLDHMA.SYS` gets shorter. The `.text` hash and the byte-compare of
  `.cold` stay: they are what makes a wrong ROM no ROM.
- **Hibernate** already keys on `[api_coldseg]`'s high byte (SPEC.md
  2.10.7). Wave 3 adds a third value, so the `COLD_HOME_HI` guard grows to
  cover the slid one too.

**Wave 2 - the floor reads the ladder instead of assuming it.**
`mem_floor_ax` and `osapi_sys_kb` learn three states: home, slid, adopted.
`SK_COLD_KB` and its siblings follow, keeping guard 7's column totalling.
Small, and it is where §5 lands if taken.

**Wave 3 - the `.vga` rung.** Move the bodies (§2), write the
`tests/vgarefs.txt` gate first so the move is checked as it is made, then
the slide. The order follows MONO-RECLAIM-PLAN section 8: the build that fails to
assemble is the only classification that cannot be fooled by a name.

**Gates and rows to name** (run in the final pass, not per wave):
`coldpic`, `rombig`, `romsmall`, `hmacold`, `kernresident`, `small128`, the
`disp*` rows on CGA and Hercules for the slide, VGA rows for the `jmp`,
`hib*` for the pad byte, `buildmatrix` for the knobs, and the fast tier's
`t_canary` and `t_blobruns` (the blob grows).

## 5. Do the rungs above LOW need to exist?

**No, and guard 6 is the authority.** Its text: *"every base an int 13h
transfer can land on is 512-byte aligned"*. It checks exactly four segments:
`KERNEL_SEG`, `FAT_SEG`, `LOW_SEG` and `HEAP_SEG`. Guard 6b adds the claims,
each `HEAP_SEG + n * 64` paragraphs. `COLD_RAM` and `VGABUF_RAM` are not in
the list, because nothing DMAs into them:
- `.cold` arrives inside stage 2's ONE read of the kernel file, which starts
  at a 512-aligned `KERNEL_SEG`, and the file carries the FAT/LOW gap as
  zeroes (`COLD_START = BOOT2_PAD + (COLD_RAM - KERNEL_SEG) * 16`). So where
  `.cold` starts inside that stream does not matter. Every transfer
  starts on a sector boundary of the stream.
- `.vgabuf` is CPU-only scratch for the planar decoder.

**So what the upper rungs are for** is keeping `HEAP_SEG` a multiple of 32
paragraphs in each state. `LOW_PARA`, `COLD_PARA` and `VGABUF_PARA` each
round up so that their SUM does, in every combination (VGA or not, adopted
or not). That is three roundings to make one number aligned. One round-up,
applied at the floor in each state, does the same job.

**What must stay aligned:**
- the image rung, because `FAT_SEG` sits on it;
- `FAT_PARA` (288, already a multiple of 32), because `LOW_SEG` sits on it;
- the heap floor, by a round-up, whichever state the machine is in.

**What can be packed to the paragraph:** everything from the top of LOW to
the floor, meaning LOW's top, the `.vga` rung, `.cold` and `.vgabuf`.
`.cold` needs 16-byte granularity because it is reached by a segment, and
nothing coarser. **Except kern_mega**, where `COLD_SEG` is a GDT selector
and must be a multiple of 8 paragraphs (mega.inc's guard). Keep its
rounding.

**What it buys today**, the honest version:

| | rungs | packed, one round-up |
|---|---:|---:|
| VGA machine, LOW+COLD+VGABUF | 44,544 (6,144 + 37,888 + 512) | 44,032 (5,872 + 37,776 + 336 = 43,984, rounded) |
| mono machine, LOW+COLD | 44,032 | 44,032 (43,648, rounded) |

**512 bytes back on a VGA machine today, 0 on mono**, and that split is
luck. Each rounding wastes 0-511 bytes, ~256 on average, so collapsing three
roundings into one is worth ~512 bytes on average and up to ~1KB. Per
CLAUDE.md's banner, the point is not today's step. Every rung's slack gets
spent by whoever crosses it next, and this removes two of the three places
slack hides. **With wave 3 it matters more**: the slide moves `.cold` by the
`.vga` rung's exact paragraphs rather than a 512-rounded figure, so a mono
machine gets the bytes back rather than the rungs.

**What it costs and touches:**
- **The ROM floor**: today `[rom_vgabw]` names `COLD_RAM`, which is
  aligned because LOW's rung is. Packed, the floor needs a run-time round-up
  (`add ax, 31` / `and al, 0E0h`, 5 bytes) in `mem_floor_ax`.
- **Guard 7 and the `SK_*` rows** (kernel.asm ~8200) assume the column is a
  whole number of 512-byte rungs. They re-derive from byte boundaries.
- **The tools that read the ladder**: `tools/kernsize.py` (its `rungs` and
  `accrued` lines collapse three rungs into one "top"), `os88layout.py`,
  `os88rom.py`, `os88romfix.py`, `os88megaseg.py`, and boot.asm's
  `HEAP_PARA`, which comes from `kernsize`'s `blobseg`.
- **The packed kernel** (SPEC.md 2.9.13): `.cold`'s file offset stops being
  512-aligned. The stream expands as bytes, so nothing should care. Check
  `tools/os88kz.py`'s block cut first: it is the one place an alignment
  assumption could hide.
- `KERN_BUDGET` still bills in 512-byte steps, because `HEAP_SEG` still
  moves in them. The budget's unit does not change; the slack under it does.

**Recommendation:** take §5 inside wave 2, before wave 3. It is a small,
separable change with its own A/B (a `RUNGTOP=1`-style knob, or simply the
before/after `kernsize`), and wave 3's slide is cleaner on a packed top.

## 6. New guards

- **The window**: `(VGA_RAM - KERNEL_SEG) * 16 + VGA_SIZE <= 65536`, i.e.
  the `.vga` rung stays near-reachable. It is the new binding form of
  `KERN_CODE_MAX`.
- **`vgarefs`**: every reference into `.vga` from outside it is guarded by a
  `.text` door, per `tests/vgarefs.txt` (§2).
- **The floor**: in each state the floor is a multiple of 32 paragraphs,
  replacing the per-rung `% 32` checks that §5 retires.

## 7. Open questions

1. **Re-take the no-VGA measurement on a build that boots.** The table in §3
   comes from a scratch assembly with two stubs. Wave 3 produces the real
   figure as its first step.
2. **Is ~105 ms of mono boot acceptable?** The alternative is expanding
   `.cold` straight to its final address, which needs the video verdict
   before stage 2's decode. That is the same lack of stage-2 room that holds
   SPEC.md 2.10.6's "skip the expand", so it is not the first thing to try.
3. **Other conditional code in the same slot.** Any code that is dead when a
   given device is absent can sit between LOW and COLD on the same terms,
   with `.cold` slid down by the total. One candidate is already eliminated:
   `NO_PS2` moves no resident byte, because the aux-port probe lives in the
   boot overlay. Nothing else has been measured.
4. **kern_small** gains nothing from wave 3 (it has no VGA), but its ROM
   gains wave 1's module space.
