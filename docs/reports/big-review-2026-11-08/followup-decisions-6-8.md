# Decisions 6, 7 and 8, explained

Follow-up to `BIG-REVIEW-2026-11-08.md` section 10, items 6-8, for the
owner's questions. Every figure here was re-read from the source and from
`build/kernel.map` at the tree the review ran on. Nothing was built and
nothing was run. Two of the review's numbers move as a result: item 6's
mouseless-machine cost, and item 8's mono figure, which is now computed
rather than estimated.

---

## 6. rt-7: the keyboard mouse at init lifetime

### What the keyboard mouse is, and why it stops being needed

The keyboard mouse (`kbm_*`, kernel/mouse.inc:2451 onward) lets a machine
with no pointer move the arrow with the keypad, click with Space or Ins, and
use keypad 5 as the button. Its own header prices it at about 456 resident
bytes and calls it "a FIRST CANDIDATE TO REMOVE" (mouse.inc:2418-2433).

Once a real pointer has proved itself it is dead code for the rest of the
session:

- `[mou_ptr]` is set by `MOU_SETTLE_STORES` (mouse.inc:1170-1181; kern_big
  calls it through `mou_settle`, mouse.inc:1184-1189, and kern_small inlines
  it at mouse.inc:3941). kern_emu's VMware path sets it separately
  (vmmouse.inc:339).
- Nothing ever clears it. SPEC.md 9.6.1.1 says so, and the declaration says
  the same (mouse.inc:6452).
- Every entry into the keyboard mouse tests it first:
  - `kbm_key` (mouse.inc:2469-2470);
  - the ISR's keypad-5 latch inside `KBM_SCAN` (mouse.inc:2878);
  - `kbm_ui`'s call site (ui.inc:720).

### What the proposal does

1. Cut the body out of the kernel into a kernel module. This is the same
   mechanism CTRL.DRV and the other modules use (kernel/mod.inc:1-38: a
   section of this assembly, cut out by `os88mod.py`, and entered through
   `call far [mod_fp+K]` thunks).
2. Load it at boot, so no disk read happens at the moment the mouse turns
   out to be missing. That was the objection to input-12's "load on first
   press".
3. Free it the moment `[mou_ptr]` is set. That means a `mod_drop` at each
   site that stores `[mou_ptr]`: `mou_settle`, kern_small's inline copy, and
   `vmm_boot_x`.

Some code has to stay resident. `kbm_isr` is IRQ1's handler for the whole
kernel. Also kept: the keypad-5 latch and the `kbm_p5` byte it sets,
`kbm_key`'s two gates, `kbm_shf`, and `kbm_pass`. **One correction to WP3's
keep list:** `kbm_poll0` must stay too (mouse.inc:3476-3500).

- `menu_track`, `ui_drag` and `ui_grow` call it on **every** machine,
  because it eats the keys a modal loop would otherwise leave wedged in the
  BIOS buffer, and then tail-jumps to `kbd_ovspend`'s overrun drain.
- It also calls `kbm_p5spend` on every machine, so that routine either
  stays resident or gets a `[mou_ptr]` gate in front of its thunk.

### The three numbers you read, and what each one measures

They are three different quantities on different machines. They are not
one ledger to add up.

| figure | what it is | where it lands |
|---|---|---|
| **-290..-380** | static resident kernel bytes: the 456 measured, less what stays resident and the new glue | every kernel; kern_big's and kern_small's `KERN_BUDGET`, kern_mega's conventional `MEGA_RESIDENT` |
| **+400..+450** | the module image: bytes on the floppy, not RAM | every system disk, small360 included: one more cluster as `KBMOUSE.DRV`, or that much more `KERNEL.SYS` if it rides there |
| **+85..+165** (corrected below to **+130..+220**) | heap held for the whole session on a machine that **never** settles a pointer | mouseless machines only |

**Where the 456 comes from.** `build/kernel.map` gives these spans:

| span | addresses | bytes |
|---|---|---:|
| `kbm_tab` .. `kbm_isr`, less `mou_clamp` | 0x43E9-0x455F (374), less 0x451B-0x455F (68) | 306 |
| `kbm_p5spend` | 0x46CC-0x46DC | 16 |
| `kbm_pollm` .. `mou_evfill` | 0x46DC-0x4748 | 108 |
| `kbm_post` | 0x475F-0x4768 | 9 |
| state, `kbm_p5` .. `kbd_ae` | 0x476E-0x477F | 17 |
| **total** | | **456** |

**Where -290..-380 comes from.** The bytes that leave are 456, less what has
to stay resident:

- `kbm_poll0` 39;
- `kbm_shf` 10;
- `kbm_key`'s gates about 20;
- `kbm_p5spend`'s test about 9;
- `kbm_pass` 2;
- `kbm_p5` 1.

That is about 80 bytes kept, so about 375 move. Against that, the new glue:

| glue | bytes |
|---|---:|
| a `mod_tab` row (`MODR_SIZE` 4, mod.inc:142-144) | 4 |
| the file name `'KBMOUSE.DRV',0` in `.text` | 12 |
| `mod_fp` slots, 4 bytes of `.bss` each, about 4 entries (SPEC.md 2.8.1, mod.inc:94-99) | 16 |
| thunks | 15-30 |
| the drop call at each settle site | about 7 each |

Glue is about 55-75 bytes, so the net is **about -300..-320** on kern_big.
The range holds, but **its -380 top is optimistic**, because `kbm_poll0`
cannot go. The figure is ESTIMATED: nothing is built.

**Where +400..+450 comes from.** The roughly 375 bytes that move, plus the
module header (`MOD_H_ENT`, 12 bytes plus 2 per entry, mod.inc:105-127),
plus the module's own far exits back into `.text` (2 bytes more per call
than a near call). PKGZ may pack it smaller on the disk.

**The mouseless-machine cost is understated in the report. The heap granule
is 512 bytes, not a paragraph.**

- `mod_need` sizes the claim with `mem_bytes_gp_x`, "-> paragraphs, whole
  granules" (mod.inc:454), and claims it top-down (`mem_claimp_hi_x`,
  mod.inc:461; "a module's base IS its CS, so it can never move").
- `MEM_GRAN` is 32 paragraphs = 512 bytes (memory.inc:468-469, memory.inc:5).
- So a ~450-byte image holds **512** bytes of heap for the whole session,
  against the 290..380 it took out of the kernel. The net is **+130..+220**,
  not +85..+165.
- If the image ever passes 512 bytes, it takes two granules and the net
  becomes about +650.

It lands on a mouseless machine, often the 128KB 5150, which is the one
with the least heap to spare. `KERN_BUDGET` cannot see it, because it
counts the static span only. `tests/kernresident.py` and `small128.py` see
it only if their test machine has no mouse.

On every machine **with** a mouse, the module's 512 bytes are held from
boot until the pointer settles: a few seconds for a serial mouse, or until
its first packet. After that the heap gets the full ~300 back for the rest
of the session.

### Two dependencies the decision carries

- **"Load at boot" has no cheap mechanism yet.** The plan rides on WP2's
  boot cache (rt-2): an image carried inside KERNEL.SYS's one contiguous
  read. That mechanism is not built, and it is decision 4's subject.
  Without it, the boot load is a `mod_need` of a file: a directory lookup
  plus a read, 1-3 `int 13h` calls, so **about 0.4-1.2 s added to every
  boot on an XT**.
- On kern_mega, module images live above 1MB, so the conventional saving
  holds and the mouseless cost is 0 conventional.

### In one sentence

The keyboard mouse moves out of the kernel into a module that is loaded at
boot and freed when a mouse speaks. Machines with a mouse gain about 300
bytes of heap seconds after boot. Machines without one lose about 130-220
bytes for the whole session. Every system disk carries one more file of
about 450 bytes. It needs the boot-cache mechanism, or it costs boot time.

---

## 7. text-7: `font_run_x`'s VGA prologue into `.vga`

### What it does

`font_run_x` is the kernel's opaque text routine. Every Note Pad row, menu
item and caption goes through it.

- On a planar display (VGA or EGA) it takes `.planar` (font.inc:1825-1912).
  This prologue groups the planes once per run and programs Set/Reset, so
  the row pass can be one `stosb` a cell row (SPEC.md 6.1.10).
- It is entered by `cmp byte [vid_planes], 4 / jae .planar`
  (font.inc:1284-1285).
- For an unaligned run, `fnt_rn_edge` has a VGA arm (font.inc:2026-2051)
  that merges the two edge columns through the Bit Mask.

All of that code sits in `.text`, so it is resident on CGA, Hercules and MDA
machines, which never run it. The proposal moves it into the `.vga` rung
(SPEC.md 2.10.9). On kern_big that rung lives inside KERNEL_SEG's window,
so calls stay near. A machine with neither a VGA nor an EGA slides `.cold`
down over the rung at boot, and those bytes become heap
(`mem_floor_ax`, memory.inc:729-747).

### Where the bytes come from

From `build/kernel.map`:

- `.planar`/`.vxp0` 0x3E65 .. `.plno` 0x3EF5 = **144**;
- `fnt_rn_edge`'s VGA arm, 0x3F78 .. `.mono` 0x3FA3 = **43**;
- **187** in all.

What stays in `.text` is the doors. The 8086 has only short conditional
jumps, so each of the two doors becomes a jcc over a near `jmp`: 5 bytes in
`.text`. Each arm then needs a 3-byte `jmp` back, in `.vga`.

| machine class | net | why |
|---|---:|---|
| CGA / Hercules / MDA kern_big | **about -177** | 187 out, about 10 of doors left |
| VGA and EGA (the `KERN_BUDGET` gauge) | **+16** | both rungs are resident there, so only the glue counts |
| kern_small, kern_mega | 0 | kern_small has no planar prologue (font.inc:1286-1295); not proposed for kern_mega |

The review's "-165..-185" brackets this figure. The +16 is the cost lens's
correction of text.md's +10.

### What it costs on VGA, in clocks

On an 8088, a near `jmp` is 15 clocks. A taken `jcc` is 16, and an untaken
one is 4. The 8-bit bus adds about 4.34 clocks per instruction byte fetched.

**Aligned run** (Note Pad's, below). This is one door.

- Today: a taken `jae` = 16 clocks.
- Moved: an untaken `jb` (4) plus a near `jmp` (15) and three more bytes to
  fetch.
- Extra: **about +5..+15 clocks a run**.
- `.planar` already returns to `.rowb` with a near `jmp` (font.inc:1911),
  so the way back costs nothing new.

**Unaligned run.** `fnt_rn_edge` is called twice (font.inc:1760, 1772).

- Each VGA call gains an untaken `je` plus a near `jmp` into `.vga`, about
  +15..+20.
- Extra: **about +35..+55 clocks a run** in all.
- The review's "~30-40" is this case.

At 4.77 MHz a clock is 0.2096 us:

| clocks | time |
|---:|---:|
| 15 | 3.1 us |
| 40 | 8.4 us |
| 70 (a pessimistic ceiling) | 14.7 us |

### The Note Pad worst case

**How Note Pad paints.** Every visible row is accumulated into `np_rbuf`
and drawn as **one** opaque `font_run`. The call is `np_rflush` at
apps/notepad/notepad.asm:2620-2633, with CBLACK on CWHITE, a single-pass
pen (SPEC.md 6.1.10's census). A full repaint white-fills the band first,
then draws one run per row, trimmed at the last non-space
(notepad.asm:2591-2614).

- `NP_MARGIN` is 8, and `OSAPI_WM_SNAP` puts the content origin on a
  multiple of 8 (notepad.asm:330-339), so Note Pad's runs are **aligned**.
  They take the cheap door, not the two edge doors.

**The maximized window on 640x480 VGA.** `wm_zoom` fills the desktop band:
full width, from `MBAR_H` = 20 to one pixel short of the 24-pixel dock
(SPEC.md 11.95; kernel.asm:44; dock.inc:57).

- A full-width window has no left border (`wm_content`, wm.inc:5677-5686),
  so the content left is 0, the content top is 20 + `TITLE_H` 18 = 38, and
  the content bottom is about 453.
- `np_bounds` (notepad.asm:1525-1592) gives:
  - text x 8, text y 46;
  - right limit 639 - `NP_SB_W` 14 = 625;
  - **77 cells** a row: (625 - 8 + 1) / 8;
  - **51 rows**: (453 - 46 - 7) / 8 + 1.
- That is 3,927 cells, completely filled, drawn as **51 `font_run` calls**.
  A fullscreen (WF_FULL) Note Pad is 59 x 77, and the percentages come out
  the same.

**What one row costs on this machine.** The CLAUDE.md figure (~71 ms for a
78-cell row, ~900 us a cell) is the general glyph-cell price. The measured
VGA price for exactly this call is lower, which makes the overhead a bigger
share. Both are shown below.

- SPEC.md 6.1.10, PERFORMANCE.md Set 76: `one full-width row` on VGA is
  **17,529 us** for about 78 lettered cells.
- From `FONT_RUN 10 aligned` 2,993 us and `FONT_RUN 20 text` 5,117 us:
  about **212 us a cell** plus about **870 us fixed a run**. 870 us is about
  4,150 clocks, which is the "~4,300-cycle prologue".

**Full repaint, 51 runs, 3,927 cells:**

| extra a run | added to the repaint | % of the measured VGA cost (51 x 17.53 ms = 0.894 s) | % of CLAUDE.md's figure (51 x 70.1 ms = 3.58 s) |
|---:|---:|---:|---:|
| 15 clocks (Note Pad's real, aligned case) | 0.16 ms | 0.018% | 0.004% |
| 40 clocks (the review's figure) | 0.43 ms | **0.048%** | 0.012% |
| 70 clocks (pessimistic ceiling) | 0.75 ms | 0.084% | 0.021% |

That is under one ten-thousandth of a frame budget per row. Each row gains
8.4 us against about 17.5 ms.

**Absolute worst case: one run per cell, the same 3,927 cells.** A one-cell
VGA run costs about 870 + 212 = **1,082 us**, so the page costs about
**4.25 s**.

| extra a run | added | % of 4.25 s (measured VGA) | % of 3.53 s (CLAUDE.md, 900 us a cell) |
|---:|---:|---:|---:|
| 15 clocks | 12 ms | 0.29% | 0.35% |
| 40 clocks | **33 ms** (about 2 frames at 60 Hz) | **0.78%** | 0.93% |
| 70 clocks | 58 ms | 1.36% | 1.63% |

Note Pad does have a one-cell run. Its typed-character append path draws
one cell opaque (notepad.asm:5920-5933). That is +8.4 us on a ~1.1 ms
draw, per keystroke.

**These clocks are PREDICTED, not measured.** SPEC.md 6.1.10 records that
`tests/gfxbench` cannot resolve a sub-1% delta between two kernels, so a
figure this small stays arithmetic.

### The trade, plainly

- **Taken:** about 177 bytes more heap on every CGA, Hercules and MDA
  kern_big machine, the target class.
- **Paid:** +16 bytes on VGA and EGA and on the `KERN_BUDGET` gauge, and
  well under 0.1% of the worst Note Pad repaint on VGA (under 1% even with
  one run per cell).
- **Upkeep:** a `tests/vgarefs.txt` row for the door, whose guard is the
  `[vid_planes]` test `font_run_x` already makes, and moving the parity pad
  at font.inc:1839 that `t_hotpar` checks.
- **Gauge:** the gauge cannot see the saving. That is item 8.

---

## 8. rt-5: gate the mono residency figure, or only print it in kernsize

### The problem

Rule 1 is checked against one machine class. Guard 1 is
`KERN_SIZE > KERN_BUDGET + KERN_GRACE` (kernel.asm:9494):

- `KERN_SIZE` is `(KERN_END - KERNEL_SEG) * 16` (kernel.asm:1921);
- `KERN_END` is the top of the ladder with **every** rung present;
- docs/KERNEL-MEMORY.md says the rule is stated "on VGA (the adapter that
  does not drop a rung of its own, so the rule is stated against the
  configuration that costs the most)".

The boot test does the same. `tests/kernresident.py` runs on
`os8088_xt_vga_128k` (line 43) and takes the kernel's span from the
assembly constant `KERN_END` (line 78), not from the running heap floor.

That is correct as a **ceiling**: no class is bigger than VGA today. But the
machines the OS is built for are not VGA. They run a smaller kernel than the
one the gauge reads, and nothing reports how much smaller. So:

- a saving that only those machines get (the `.cold` slide, text-7's -177,
  video-17's mono rung) never shows on the gauge;
- a change that moves code from `.vga` into `.text` costs those machines
  bytes and reads as zero on the gauge.

### The figures, from the build constants (MEASURED, no estimate)

The ladder under `COLD_SLIDE` and `LADDER_PACK` (kernel.asm:1680-1697,
1852, 1910-1921):

```
KERNEL 0x0060 | image + FAT + LOW | VGA rung 0x0E2F (+0x07F) | .cold 0x0EAE (+0x92C) | .vgabuf 0x17DA (+0x015) | KERN_END 0x1800
```

`mem_floor_ax` (memory.inc:729-774) gives each class its heap floor.

| class | heap floor | kernel resident | against the gauge |
|---|---|---:|---:|
| VGA (the gauge) | 0x1800 | **96,768** | - |
| EGA (`.vgabuf` not reserved, but the floor rounds up to 512 again) | round(0x1800 - 0x15) = 0x1800 | 96,768 | 0 today |
| **CGA / Hercules / MDA**: `.cold` slid over the VGA rung | `MONO_FLOOR` = round(0x0E2F + 0x92C) = 0x1760 | **94,208** | **-2,560** |
| mono with `.cold` in a ROM or the HMA | round(0x0E2F) = 0x0E40 | 56,832 | -39,936 |

The review's "~94,400-94,720" for mono was an estimate. The constants say
94,208. The kernel already computes this number:

- `MONO_FLOOR` is defined at kernel.asm:9374;
- it feeds `SK_MONO_KB`, the RAM-view row that `osapi_sys_kb` reports
  (kernel.asm:9370-9376).

So the figure exists inside the assembly. It is just never printed to a
developer and never checked.

### The two options

**(a) Only print it.** In `tools/kernsize.py`, add a `classes` line beside
the `footprint` and `boot` lines (kernsize.py:630-667): VGA, EGA, mono, and
mono with ROM or HMA. Bless it into docs/KERNEL-MEMORY.md's baseline so every
`make` shows the delta per class.

- It needs no new input. The `ks:` line already hands kernsize `vgars`,
  `coldpara`, `vgabufpara` and `kseg` (kernel.asm:9396-9447), which are
  everything above.
- Or add `mono=MONO_FLOOR` to that `%warning`. It is a `-DKERNSIZE`-only
  line and changes no byte of the kernel.
- **0 shipped bytes.** Host Python only.
- Effect: text-7 would read "+16 VGA, -177 mono" in every report, instead
  of "+16".

**(b) Gate it.** Add a second rule, for example a `KERN_MONO_BUDGET`, and a
guard `%if (MONO_FLOOR - KERNEL_SEG)*16 > KERN_MONO_BUDGET + KERN_GRACE`
beside guard 1. It is 0 shipped bytes as well, being assembler arithmetic.
Optionally add a soak-tier twin of `kernresident` on a Hercules machine.

- **What it would catch today is narrow.** Mono is VGA minus the VGA rung
  and `.vgabuf`, so anything that grows the mono kernel also grows the VGA
  one and guard 1 already fires. The one exception is a byte moved **out
  of** `.vga` into `.text` or `.cold`: VGA-neutral, a pure mono loss.
- **It starts to matter when the classes become complementary.** That
  happens if a mono-only rung lands (video-17: smaller on VGA, larger on
  mono, rt-4's lattice). Then the VGA gate is no longer the maximum, and an
  unchecked mono figure could grow past what the target machines can hold.
- **It is a new budget, so it is a policy.** It is a number to set, met
  exactly at a merge like the others, and re-agreed whenever a mono win is
  banked. Otherwise the slack a mono win creates is spent by whoever comes
  next, which is the banner's point about slack.
- **The Hercules twin of `kernresident` cannot be the row as it stands.**
  Line 78's `span = KERN_END * 16` would read the VGA figure on a Hercules
  machine and pass: green, and testing nothing. A mono twin has to read the
  running floor, `[mem_base]` (memory.inc:6312), instead.

### What the review recommends, and why

Do (a) now: zero bytes, and it makes text-7, the slide and every `.vga`
move visible in the units the target machine pays in. Treat (b) as the
owner's policy question. It earns its keep when the first mono-only rung
lands, or when you want the target machines' wins kept from being quietly
re-spent. Before then it can fire on only one shape of change.
