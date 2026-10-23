# KERN-MEGA-SEGMENTS-PLAN — a segment of kern_mega's own

**Status: W0 to W4 ARE BUILT** (W0-W3 on 2026-10-20, branch
`protected-mode-segments`, cut from `protected-mode` at `7c64410`; W3's rest
and W4 on 2026-10-21, KERN-MEGA-PLAN 14.45, which is 8.7 here); section 8
is what was built and measured, and what is still owed. The owner's answers
to section 6 are in 8.1. The owner asked: kern_mega still has
kern_big's two segments, it does more, and it will need room to grow. Does it
get its own segment(s), and how much kern_mega-only code can leave the low 64KB
segment so the next time kern_big grows it does not overflow kern_mega?

Every byte figure below is MEASURED off the two `[map all]` maps at build 248
(`tools/os88megaseg.py` re-derives the per-section and per-file ones in a
second; 7 says how the rest were taken). Every cycle figure is from Intel's
80286 datasheet and is **not** measured here.

---

## 0. The answer

1. **kern_mega has 116 bytes left in its kernel segment.** `.text` + `.bss` is
   **65,420 of 65,536**. kern_big's is 48,121, with 17,415 left. The two
   share every shared file, so **every byte kern_big adds to `.text`/`.bss` is
   a byte off kern_mega's 116**. The next kern_big change that adds 117 bytes
   there breaks `make mega`, and nothing on the kern_big side can see it
   coming.
2. **There is a second wall, and it is nearly as close.** `.cold` runs above
   1MB from `MEGA_COLD_LIN` (0x125000), and the XMEM pool starts at
   `MEGA_XPOOL` (0x130000). That slot is 45,056 bytes and `.cold` is 41,809,
   so **kern_big's `.cold` growing by 3,248 bytes breaks kern_mega too**.
   kernel.asm already has the `%error` for it.
3. **What kern_mega adds to the segment is 17,299 bytes, and 16,193 of them are
   `mega.inc` and `megados.inc`.** Shared files' own `%ifdef KERN_MEGA` arms
   add +472, macro-made labels +555 (the per-slot API stubs are 344 of
   them), `.bss` +79 (1.1).
4. **Yes: kern_mega gets ONE segment of its own, `.mega`.** It is kern_mega-only
   code that runs only in protected mode, under a fixed selector that names no
   conventional segment (3.1). Its bytes ride the kernel image after `.cold`
   and go above 1MB with it at `mega_enter`. One segment, not several: it would
   start about a quarter full, and a second one is the same mechanism again.
5. **About 12.4KB moves in the first bulk wave and another ~3KB in the second**
   (3.6, 4). The code that must stay low is the code that runs in REAL mode
   after the switch: the BIOS reflection's real-mode half, the 286 reset
   landing, the IRQ capture, and the DOS drop. That is ~1.9KB. What stays in
   the segment for good is that, plus writable state, toast strings, the slot
   stubs and the shared files' arms.
6. **What it buys, after wave 2:** kern_mega's segment goes from **116 bytes
   left to ~12,300**. kern_mega then overflows only when kern_big is within
   ~5,100 bytes of its OWN wall (~3,400 after wave 3), and W0's ratchet makes
   that a number that can only go down (4, W0). `.mega` keeps ~49KB of room
   for kern_mega's own growth, even after W3. **kern_mega machines also get ~12KB more
   conventional heap**: the kernel rung shrinks from 65,536 to 53,248 bytes,
   and the `.mega` rung goes back to the heap with `.cold`'s.
7. **What it costs:** **64KB of the XMEM pool** (`MEGA_XPOOL` 0x130000 →
   0x140000, which the `.cold` wall needs anyway); ~200 bytes of far doors in
   `.text` (counted in 6); one GDT entry; and teaching four tools the new
   section. kern_big stays **byte-identical**, because every line is inside
   `%ifdef KERN_MEGA`.

---

## 1. Where kern_mega's segment goes

### 1.1 The segment, kern_mega against kern_big

| section | kern_big | kern_mega | delta | what the delta is |
|---|---:|---:|---:|---|
| `.text` | 42,866 | 60,086 | **+17,220** | `mega.inc` 12,053; `megados.inc` 4,140; macro-made labels +555, of which the per-slot API stubs (`mega_sl_*`) are 344 and `mega_icw*` 110; shared files' `%ifdef KERN_MEGA` arms (`snd.inc` +112, `kernel.asm` +103, `font.inc` +52, `mouse.inc` +49, `sched.inc` +46, `menu.inc` +41, `wm.inc` +38, seven more under 20, `xmem.inc` −38; net +472) |
| `.bss` | 5,255 | 5,334 | **+79** | `mega.inc` 47, `driver.inc` +24 |
| **window** | **48,121** | **65,420** | **+17,299** | **116 left on kern_mega, 17,415 on kern_big** |
| `.cold` | 37,469 | 41,809 | +4,340 | `mega.inc` 1,691; `megados.inc` 76; shared arms in `memory.inc` +622, `disk.inc` +594, `driver.inc` +593, `diskw.inc` +205, macro-made +209, `loader.inc` +143, `mod.inc` +109, smaller ones +98 |

`.cold`'s ceilings are different on the two kernels. On kern_mega it starts at
offset 0x4000 (`HMA_COLD_AT` under `KERN_KNOB`), so it may reach 48,896 bytes
before the 0xFF00 guard. Above 1MB, though, it has only the 45,056 between
`MEGA_COLD_LIN` and `MEGA_XPOOL`, which leaves **3,247**. KERN-MEGA-PLAN 14.10
was the last time the segment ran out: it moved the claim family to `.cold`
for 364 bytes. Two days of merges later the 364 is down to 116.

### 1.2 What `mega.inc` and `megados.inc` hold in `.text`

Of the 16,193 bytes, 13,677 are code and 2,516 are data. Grouped by what the
code is for:

| group | bytes | runs |
|---|---:|---|
| the BIOS reflection and its real-mode half (`mega_svc`, `mega_ss`, `mega_r286`, `mega_capon`/`capoff`/`cap2`/`capc`, `mega_fdrprobe`, `mega_pic_pm`, `mega_kbw`, `mega_tramp`) | 1,209 | **both modes** |
| the DOS drop (`mega_dosgo`, `mega_hbgof`, `mega_dropr`) | 633 | protected mode, then **across the switch** |
| `sched.inc`'s near hooks (`mega_frame_ds`, `mega_frame_cs`) | 39 | protected mode, on the task switch |
| ISRs: tick and keyboard halves, int 16h, NMI, the floppy's IRQ, the IRQ-reflection pre/post | 1,650 | protected mode |
| faults and fixups: exception stubs, the reporter, `#GP` write and null-segment fixups, the string-restart calibration | 2,132 | protected mode |
| the native floppy (`mega_fd13`, `mega_fdgo` and helpers) | 1,610 | protected mode |
| the pool, the LDT allocator, selectors, claim doors (`mx_*`, `mega_ldt_*`, `mega_xmsel`, `mega_codesel`, `mega_*hif*`) | 2,369 | protected mode |
| the switch itself and its once-only setup (`mega_enter`, `mega_idt_*`, `mega_coldup`, `mega_xminit`, the A20 tests, `mega_frames_fix`, `mega_mods_fix`) | 1,850 | once, at boot |
| E9 and menu-bar reporting (`mega_tell`, `mega_say`, `mega_puts`, the hex printers, their strings) | 693 | protected mode |
| the DOS park, planning half (`mega_dosparkf` 977, `mega_dosclaimf`, `mega_dsumall`, `mega_dmapb`, `mega_dwake`, …) | 2,433 | protected mode |
| writable state, the region list `mega_dreg` (716), toast strings | ~1,520 | data, through DS |

(The groups are hand-assigned from the routines' names and their callers, so
their edges are approximate. The totals are exact.)

**The finding that makes this cheap:** kern_mega's code calls back into shared
`.text` near only **four** times, to `toast_show` (three times) and `font_run`
(once). It reads kernel data through DS, which is the kernel's data selector
from whichever code segment it runs in. And almost every entry from shared
code is ALREADY far: `.cold` reaches it as `KERNEL_SEG:mega_*f`, the 4-byte
`call x / retf` doors 14.10 built. So moving a body changes the segment in
its callers and nothing else.

---

## 2. Alternatives, and why each is refused or kept as a stopgap

**A. Another size pass on kern_mega's `.text`.** That is 14.10's move, and it
bought 364 bytes. The deficit is 17,299 and it grows with every
`%ifdef KERN_MEGA` anyone writes. A size pass does not change which way the
segment is going. Refused as the plan; useful as the bridge in 5.

**B. Put kern_mega's code in `.cold` instead.** Lower kern_mega's
`COLD_VSTART` to 0x10, raise `MEGA_XPOOL`, and move the same ~12.4KB with
`section .cold`. This is CHEAPER than `.mega`: no new selector, no new rung,
no new copy, and `os88sym` and the park sum already know the section. It
works, and it is **the stopgap if a merge overflows before W1 lands**. As the
design it is refused, for three reasons:

* it couples the two kernels' growth in the second segment exactly as they
  are coupled in the first today. kern_mega's `.cold` would be kern_big's
  37.5KB plus ~18KB of its own, so kern_big's `.cold` growth eats kern_mega's
  room again, just with a larger margin;
* `.cold` also runs in REAL mode before the switch, so it cannot carry 3.2's
  contract "nothing here runs in real mode", which is what makes a fault in
  kern_mega's own code easy to place;
* the owner's premise is that kern_mega will grow. A segment that is
  kern_mega's alone is the shape that lets it.

**C. Several segments now** (`.mega` for drivers, `.megados` for the park,
…). Each costs a selector, a rung, a copy and a park region. One segment that
starts a quarter full does not need splitting. A second one is 3's mechanism
again, and it is worth doing when `.mega` passes ~48KB.

**D. An on-demand module** (SPEC.md 2.8, `mod.inc`). Refused. This code is
RESIDENT: ISRs, fault handlers, and the floppy driver that `mod_need` itself
loads through in protected mode. A module that is never dropped is the same
bytes in a different account (O88-MULTISEG-PLAN 1 says the same about the
kernel-side version of that idea).

**E. Move SHARED code out of kern_mega's segment** — say, `%ifdef KERN_MEGA`
a shared routine into `.mega`. Refused. That makes the two builds disagree
about where shared code lives, which is the "permanent cost of overlap"
KERN-MEGA-PLAN 7 warns about, in its worst form.

---

## 3. The design

### 3.1 The section and its selector

```
%ifdef KERN_MEGA
section .mega   start=MEGA_START vstart=0
%endif
```

`.mega` is code with the `.cold` contract: CS is `.mega`, DS is the kernel's
data selector, SS is `LOW_SEG`. A routine entered from outside is far and
`retf`s; calls inside the segment are near.

**Its CS is a fixed selector, `MEGA_SEL_CODE` = 0xFFD8**: GDT entry 1FFBh,
the identity selector of ROM segment FFD8, which nothing loads. That is the
same argument as `MEGA_SEL_SLD` (0xFFE0), `MEGA_SEL_LDTD` (0xFFE8) and
`MEGA_SEL_XA`/`XB` (0xFFF0/0xFFF8). 0xFFC0–0xFFD8 are unused today. An
optional data alias at 0xFFD0 is there for anything that must write its own
image; 3.6 says what might.

**It must NOT reuse `.cold`'s trick** of making the identity entry of its own
real-mode segment into code (`COLD_SEG`). That works for `.cold` because
`COLD_SEG` is its rung LESS 0x400 paragraphs (`COLD_VSTART` 0x4000), which
puts it inside `.lowbss`, where no heap claim can ever start. `.mega`'s rung
is given back to the HEAP at the switch (3.3), and heap bases are multiples of
32 paragraphs. A `.mega` named by its own rung's segment would, sooner or
later, be the selector a package's claim is handed, and that claim would be
CODE. A fixed ROM-top selector cannot collide with anything.

That choice is possible only because of the next rule.

### 3.2 The contract: nothing in `.mega` runs in real mode, ever

Every entry into `.mega` is reached only with `[mega_on]` = 1:

* IDT gates `mega_enter` writes;
* the far chains it installs (`[sch_old08]`, `[kbm_old9]`);
* far calls from code that runs only in protected mode, or that tests
  `[mega_on]`/`[ss:mega_pm]` first.

This is what lets `.mega` have no real-mode name at all. It also makes a fault
report easy to read: a CS of 0xFFD8 is kern_mega's own code and nothing else.

The rule has a consequence for `mega_enter` (3.6). The bootstrap, between the
first `lmsw` and loading the real GDT, runs under `MEGA_SEL_BCODE` with the
base of `KERNEL_SEG`. So it stays in `.text`. The real-mode refusals before it
(`mega_tasks_ok`, the A20 tests, `mega_tlowf`) can stay in `.text` or move to
`.cold`. `.cold` still runs low and in real mode at that point, as
`mega_tlowf` already does.

### 3.3 Where its bytes live

**At boot.** Today's ladder is `KERNEL | FAT | LOW | COLD | VGABUF | HEAP`.
kern_mega keeps ROM_COLD's ladder, with `.cold` on the top rung (kernel.asm's
note at line 715). `.mega` becomes the rung above it:

```
KERNEL | FAT | LOW | COLD | MEGA | VGABUF | HEAP
```

In the file it is the section after `.cold`. `MEGA_START` =
`COLD_START + COLD_PARA*16`, and `MODC_START` moves past it. The kernel file
is a memory image of the ladder, so the boot loader's one read, through the
KZIP stream, puts it in place with no loader change. The boot read is about
the same size, because the bytes left `.text` to get here (rung rounding
aside).

**At the switch.** `mega_coldup` becomes `mega_hiup`. It is ONE copy of the
`COLD` and `MEGA` rungs, which are contiguous, to `MEGA_COLD_LIN`, then two
descriptor writes:

* `COLD_SEG`'s, as today;
* `MEGA_SEL_CODE`'s, base `MEGA_COLD_LIN + COLD_PARA*16`.

The heap floor then drops onto `COLD_RAM` exactly as it does today, so the
`.mega` rung goes back to the heap with `.cold`'s at no extra cost.
`mega_lincopy`'s count is a word of bytes, so a span past 64KB is two copies.

There is one **ordering constraint**: `mega_hiup` runs before the first far
call into `.mega`. Today `mega_coldup` runs after `mega_frames_fix` and
`mega_mods_fix`. W1 has to establish whether that order matters, or simply
leave those two in `.text`.

**On a machine with under 192KB of extended memory** (KERN-MEGA-PLAN 14.17),
`.cold` stays low, and so does `.mega`. Its descriptor's base is the `MEGA`
rung, which stays allocated. That costs the same conventional bytes the code
costs in `.text` today, give or take a rung.

**On a machine that stays in real mode** (no 286, A20 refused, a task that
cannot be converted), nothing in `.mega` ever runs. W1 keeps the rung anyway,
which is safe and changes nothing against today. W3 frees it on refusal, once
every far call into `.mega` is shown to be behind `[mega_on]`. That gives such
a machine ~13KB back.

### 3.4 Above 1MB

The high kernel is `[MEGA_COLD_LIN, MEGA_COLD_LIN + COLD_PARA*16 + MEGA_SIZE)`.
`MEGA_XPOOL` moves from 0x130000 to **0x140000**. The slot grows from 45,056
bytes to 110,592: `.cold` at ~40KB and `.mega` at ~16KB leave ~54KB spare.
The pool stays 64KB-aligned, which XMEM.DRV's arithmetic and `hiber.inc`'s
`%if MEGA_XPOOL & 0xFFFF` require. Every user of the pool base names the
constant, so nothing else moves. Two guards replace kernel.asm's `.cold` one:
the high kernel fits under `MEGA_XPOOL`, and `.mega` fits its 64KB.

The cost is **64KB of the pool**. On a 286 with 1MB of extended memory that is
832KB → 768KB.

### 3.5 Every place that knows `.cold`'s high copy learns `.mega`'s

Making the two one contiguous region turns most of these into a length
change:

* **the DOS park's sum** (`megados.inc:640`). It registers `.cold`'s high
  copy as a region. The length becomes the whole high kernel, so no new list
  entry is needed and `MPK_RMAX` does not move;
* **`osapi_sys_kb`** (`memory.inc:5119`). It takes `SK_COLD_KB` off the
  image row when `.cold` runs high, and has to take the `MEGA` rung off too;
* **`MOD_STAMP`** (`mod.inc:124`). It is `.text + .bss + .cold + .lowbss`.
  Moving code from `.text` to `.mega` can leave that sum EQUAL while every
  offset moves, so a stale module would load. `MEGA_SIZE` joins the sum;
* **the hibernate path** (`mega_hbgof`, `mega_hbwake`, W5's copies). The image
  stamp already refuses a different build, so the high kernel the image
  resumes into is its own. W1 has to CHECK that rather than assume it, under
  Bochs, with a hibernate and resume on kern_mega;
* **`mega_report`**. It names a faulting CS:IP; it learns 0xFFD8, and so does
  `tools/os88bochs.py`'s resolver (docs/BOCHS.md).

### 3.6 What stays low, and why

| stays in `.text` | bytes | why |
|---|---:|---|
| `mega_svc`'s real-mode half, `mega_r286`, `mega_dropr` | ~500 (wave 3), 870 (wave 2) | runs in REAL mode after the switch: a 286 comes back through 40:67 into `mega_r286`, and the 386's path clears CR0.PE and far-jumps to `KERNEL_SEG`. Real mode cannot reach above 1MB+64KB |
| `mega_capon`, `mega_capoff`, `mega_cap2`, `mega_capc`, `mega_capsave` | 226 | the IRQ capture: IVT handlers that run in real mode while the BIOS has the CPU, and write their own image through CS (`or [cs:mega_irqpend]`) |
| `mega_fdrprobe`, `mega_pic_pm` | 107 | called by `mega_r286` in real mode. They are also called from protected mode, which far-calls them |
| `mega_ss` (the int 10h–1Fh stubs) | 128 | they jump to `mega_svc`. They could move with a far jump each, for nothing |
| `mega_dosgo`, `mega_hbgof` | ~80 (wave 3), 533 (wave 2) | their protected-mode preparation can move. Their last ~40 bytes are the drop itself (CR0 or the shutdown-0Ah reset), which has to sit at a real-mode address |
| `mega_tramp` / `mega_tif` | 23 + 13 data | `mega_gpfix` writes an instruction there through the DATA alias of `KERNEL_SEG` and runs it. It could move with `.mega`'s data alias. It is 36 bytes, so it is not worth it |
| `mega_frame_ds`, `mega_frame_cs` | 39 | near-called from `sched.inc` on the task switch. Keep near |
| the bootstrap in `mega_enter` | ~300 | runs under `MEGA_SEL_BCODE` (3.2) |
| writable state | ~800 (wave 3), ~1,520 (wave 2) | written through DS; `hiber.inc` names eight of the cells. `mega_dreg` (716), `mega_dmap` (96) and `mega_dhc` (82) are BUFFERS, and W3 builds them in the park block's own header above 1MB instead (that header already has room for the region list, `MPK_REG`) |
| toast strings | ~222 | `toast_show` keeps a pointer into the kernel's DS |
| `mega_sl_*`, `mega_fj_*` | 344 + | every hot API slot's DS stub. It must be near the slot (KERN-MEGA-PLAN 2.4) |
| the shared files' `%ifdef KERN_MEGA` arms | 472 | inline in shared routines. They are what makes them correct under protected mode |

The far calls this creates are few, and they are on slow paths:

| from | to `.mega` | how |
|---|---|---|
| `mega_svc`'s protected-mode front and back (low) | `mega_fd13`, `mega_s2seg`, `mega_pit`, `mega_pmpre`, `mega_pmpost`, `mega_replay`, `mega_say` | `call MEGA_SEL_CODE:x`, +2 bytes a site, plus a 4-byte `call / retf` door where the body is also called near. One far call against a ~400 ms `int 13h` |
| `xm_dsp`'s kern_mega arm (`xmem.inc`), `hiber.inc`'s `mov bp, mega_lincopy` | `mx_*`, `mega_lincopy` | ONE door in `.mega`, `call bp / retf`, entered far with BP = the routine. It is already an `%ifdef KERN_MEGA` arm |
| `kernel.asm`'s `call mega_enter`, `osapi_mem_codesel`'s `jne mega_codesel`, `osapi_xmem_sel`/`_walk`, `ui.inc`'s `call mega_tell`, `xmem.inc`'s `call mega_selbase` | the bodies | each site is already an `%ifdef KERN_MEGA` arm, so it becomes a far call, or a near 6-byte thunk where the site is a `jne` |
| `.cold` and `hiber.inc` (`memory.inc`, `disk.inc`, `driver.inc`, `loader.inc`, `lz.inc`, `diskw.inc`, `compress.inc`, `hiber.inc`, `mod.inc`, `deskmod.inc`, and `kernel.asm`'s two image helpers) | the far entries: 23 targets today, 21 of which move (`mega_dosgo` and `mega_hbgof` stay low until W3 splits them) | the segment in the existing far call changes, `KERNEL_SEG:` → `MEGA_SEL_CODE:`. The doors move with their bodies |
| `.mega` | `toast_show` ×3, `font_run` ×1 | far, through the existing `cw_` shims or a 4-byte door |
| the tick and keyboard chains | `mega_t08`, `mega_k09` | `[sch_old08]` and `[kbm_old9]` get `MEGA_SEL_CODE`. That is still a different CS from `kbm_isr`'s, so the `.selfchain` guard is unaffected |

Three edits inside the moved code. `mega_t08`, `mega_nmi` and `mega_hex2` load
DS with `mov ds, [cs:k_dsel]`, which would read `.mega`'s offset; each takes
the constant `MEGA_SEL_KDATA` instead. They run only in protected mode, which
is what makes the constant right. And `mega_k09`'s tables (`mega_kbase`,
`mega_kshift`, `mega_kpad`) and `mega_puts`'s strings are read through CS, so
they move WITH their readers.

**The doors and thunks are ~200 bytes of `.text`.** That is an ESTIMATE from
the count above, and it is already netted out of every figure in 0 and 4.

### 3.7 Speed and stack

The hot paths do not get a new far transfer.

* The tick and keyboard halves are reached through far chains today, under
  `MEGA_SEL_BCODE`.
* The fault handlers, `int 16h` and the floppy IRQ are IDT gates, and a gate
  loads CS whichever segment it names.
* The API slots and their stubs stay where they are.

The new far transfers are `mega_svc` → `mega_fd13` and its neighbours. At most
three of them sit round a BIOS reflection that costs **412 µs** on the Victory
286 (KERN-MEGA-PLAN 0 item 2). In protected mode a far CALL is 26 cycles and a
far RET 25, against 7 and 11 near, so each far pair costs 33 cycles more:
~4 µs at 8 MHz, ~1% of the reflection.

The stack takes 2 more bytes per low → high far call: `mega_svc`'s depth +2 on
whatever stack the reflection runs on, and the same for `xm_dsp`. Nothing
moves on the task-switch path.

---

## 4. Waves

**W0 — the instrument and the ratchet.** `tools/os88megaseg.py` (in this
commit) prints the window on both kernels, the overhead between them, `.cold`'s
high slot, and the per-file deltas. W0 adds `--check`. It fails when
kern_mega's overhead in the segment (today 17,299) is above a number kept in
the tree. That number only goes DOWN, which is `t_cswrite`'s shape. The mega
recipe runs it, because only `make mega` has both maps. From then on, a
kern_big change that would overflow kern_mega is caught by kern_mega's build,
with a sentence that names the overhead and the file, rather than by an
`%error` about `KERN_CODE_MAX`.

**W1 — the segment, nearly empty.**

* The section and selector (3.1).
* The rung, the copy and the descriptor (3.3).
* `MEGA_XPOOL` to 0x140000 and the guards (3.4).
* The five sites in 3.5.
* The tools:
  * `os88sym`, which resolves a kernel address by linear address against the
    ladder;
  * `os88ovlchk`, which gets return-kind and far-tail rules for `.mega`;
  * `t_cswrite`'s registry, where `.mega` is read-only code like the rest;
  * `os88bochs`' fault resolver;
  * `kernsize` gets nothing: it does not model kern_mega today, and W0's tool
    covers the gap.
* Move ONE group to prove it: the E9 reporting (693 bytes), whose failure
  mode is a missing debug line rather than a dead machine.

Gate: kern_big byte-identical at one build number (KERN-MEGA-HANDOFF A.2), and
`make mega`. Under Bochs (docs/BOCHS.md): boot to the desktop, a deliberate
fault reported with CS 0xFFD8, a C: → B: copy byte-identical, the MEGAHI and
MEGAXM probes, and a DOS park and return.

**W2 — the bulk, by group, one commit each** (1.2's table, ~12.4KB):

1. the pool, LDT and selector family (2,369);
2. the native floppy (1,610);
3. faults and fixups (2,132);
4. the ISRs (1,650);
5. the DOS park's planning half (2,433);
6. the once-only setup after the real GDT is loaded (~1,500 of 1,850).

Each commit gets W1's Bochs gate. Items 3 and 5 also get a field image for the
Victory 286. The `#GP` fixups' string restart (KERN-MEGA-PLAN 14.30) and the
286 reset return are the two paths Bochs cannot run (docs/BOCHS.md: its CPU
is a 386).

**W3 — the splits and the stragglers** (~1,700 more out of `.text`, and
~1,600 out of `.cold`'s high slot):

* `mega_svc`'s protected-mode front and back: ~400, leaving the real-mode core
  of ~230;
* `mega_dosgo`'s and `mega_hbgof`'s preparation: ~450;
* `mega_dreg`, `mega_dmap` and `mega_dhc`, built in the park block: 894;
* `.cold`'s own kern_mega routines, `mega_hugef`, `mega_xmrunf`,
  `mega_hibnc`, `mx_compactf`, the claim family and the rest: ~1,600 of 1,767.
  `mega_tlowf` runs in real mode before the switch and `mega_pkgok` is
  near-called from `loader.inc`, so those two stay;
* the `MEGA` rung freed on a real-mode refusal (3.3).

**W4 — optional.** Lower kern_mega's `COLD_VSTART` from 0x4000 to 0x10. No HMA
or ROM image is ever cut from kern_mega's `.cold` (kernel.asm's note at line
715), so this gives `.cold` 16KB more address room for nothing but a
renumbered `COLD_SEG`, and the collision checks catch any number that clashes.
After 3.4 it is not needed. It is listed because it is the cheapest 16KB
there is.

---

## 5. What it buys, in numbers

| | today | after W1 | after W2 | after W3 |
|---|---:|---:|---:|---:|
| kern_mega `.text`+`.bss` | 65,420 | ~64,800 | ~53,200 | ~51,500 |
| …left in the segment | **116** | ~700 | **~12,300** | ~14,000 |
| kern_mega's overhead over kern_big (W0's ratchet) | 17,299 | ~16,700 | ~5,100 | ~3,400 |
| kernel rung (conventional, resident for good) | 65,536 | 65,536 | 53,248 | 51,712 |
| high slot (`.cold` + `.mega` above 1MB), room left | 3,247 | ~67,900 | ~54,900 | ~53,800 |
| `.mega` used / 65,536 | — | ~700 | ~13,000 | ~16,300 |

Read the third row as **"kern_mega overflows when kern_big has this many bytes
left"**. Today that is 17,299, which means "on kern_big's next 116 bytes".
After W2 it is ~5,100: kern_big can grow ~12KB of its 17KB before kern_mega
feels it, and W0's ratchet fails the build if a kern_mega change pushes the
figure back up.

The figures after W1 are ESTIMATES from 1.2's measured groups less the
~200-byte door estimate; W1's and W2's own commits replace them with
measurements.

**Until W1 lands, the stopgap** for a merge that overflows is 14.10's move:
take a group of far-entered routines into `.cold`. That is 2's alternative B
in miniature. `.cold`'s high slot has 3,247 bytes for it, and W1 raises that
slot.

---

## 6. Open questions for the owner

1. **64KB of pool for the high slot** (3.4). Take it, or make `MEGA_XPOOL`
   follow the high kernel up in 64KB steps? Following costs the same 64KB
   today, plus a `%if` that picks the step.
2. **The selector, 0xFFD8.** It names no segment anything loads, by the same
   argument as the four scratch selectors beside it. Any objection to growing
   that family?
3. **Free the rung on a real-mode refusal (W3)?** It gives ~13KB back to a
   machine that refused protected mode. It needs an audit that every
   `MEGA_SEL_CODE:` call site is behind `[mega_on]`.
4. **The ratchet's starting number** (W0): today's 17,299, or the after-W2
   target once W2 lands?
5. **The Victory 286 runs**: W2 items 3 and 5, and anything touching
   `mega_r286`'s neighbours, are what only it can check.

---

## 7. How the figures were taken

* **Per section and per file**: `python3 tools/os88megaseg.py`, after `make`
  and `make BUILD=build/megak KERN_MEGA=1 build/megak/kernel.bin` at one
  commit (both build 248). Sizes are label-to-next-label in each map. They
  sum to each section's length exactly, and are filed under the file that
  defines the label. `--labels` lists kern_mega's own labels.
* **Code against data, the call graph, and "runs in real mode"**: from the
  same sizes, with each top-level label's body in `mega.inc`/`megados.inc`
  scanned for `call`/`jmp`/Jcc targets, `[cs:` operands, and references from
  every other kernel file. A label whose body is only `db`/`dw`/`times` is
  data. The real-mode set is the closure from the four real-mode entries:
  * `mega_r286`, the 40:67 landing;
  * `mega_svc`'s `.rm` half;
  * `mega_dropr`;
  * the IRQ capture's IVT handlers.

  The scan was done in a scratch script and is not checked in; its groups in
  1.2 are hand-assigned.
* **Speed**: Intel 80286 datasheet, not measured. **The reflection's cost** is
  KERN-MEGA-PLAN 0 item 2's field figure.

---

## 8. As built (2026-10-20)

### 8.1 The owner's answers to section 6

1. **The pool gives up 64KB.** `MEGA_XPOOL` is 0x140000.
2. **The selector is the plan's**, 0xFFD8 (`MEGA_SEL_CODE`).
3. **The rung is freed when protected mode is refused**, and it is not
   important: a machine that refuses runs as kern_big with a little code
   appended, and supporting that is not a requirement.
4. **The ratchet starts where the work ended** (W0 below), and is raised, with
   a reason, only for code that has to run in real mode after the switch.
5. **As much as possible lands before the next Victory runs.**

### 8.2 What it measures now

Off the two maps at one commit (`tools/os88megaseg.py`):

| | before | after |
|---|---:|---:|
| kern_mega `.text` + `.bss` | 65,420 | **54,052** |
| ...left in the 64KB segment | **116** | **11,484** |
| kern_mega's overhead over kern_big there (the ratchet) | 17,299 | **5,931** |
| `.cold` | 41,809 | 41,101 |
| `.mega` | - | **12,578** |
| high slot (`.cold`'s rung + `.mega`) left before the pool | 3,247 | 56,542 |
| the kernel's resident rung in conventional memory | 65,536 | 54,272 |

So kern_mega overflows its kernel segment only when kern_big's own `.text` +
`.bss` grows by another 11,484 bytes, where it was 116, and the conventional
heap of a kern_mega machine that copies `.cold` and `.mega` above 1MB is
**11,264 bytes larger** - the resident rung's shrink. That last figure was
ARITHMETIC off the ladder (`KIMG_PARA`) when this was written; 8.7 has the
reading, and it is the same 11,264 to the byte on both emulators.

kern_big is **byte-identical** (`kernel-full.bin`, modules included, at build
1600). `.mega` is assembled only with `-DKERN_MEGA`.

### 8.3 What moved, and what stays low

**Into `.mega`** (section 3.6's groups, by commit):

* the pool, the LDT allocator, the selector family and the claim doors;
* the native floppy, with the 765's byte-level helpers and the reset probe
  emitted twice from one macro (`MEGA_FDIO`): `mega_r286` probes in real mode,
  so `.text` keeps a copy and the native path calls its own;
* the faults: the exception stubs, the reporter, the `#GP` write and
  far-transfer fixups, the null-segment step, the string-restart calibration;
* the ISRs: the tick's and the keyboard's ROM halves (their chains now name
  `MEGA_SEL_CODE`), int 16h, NMI, the floppy's IRQ, the EOI stubs;
* the menu bar's and the debug port's reporting, with a second copy of the E9
  writers (`MEGA_E9IO`, `mm_*`) that reads its strings through `.mega`'s CS;
* the DOS park's planning, its wake (`mega_dwake`; the park stub now accepts
  `MEGA_SEL_CODE` as the wake's segment), the hibernate's go and wake, and the
  park header's helpers;
* `mega_enter`'s protected-mode tail (`mega_pmtail`), after `mega_coldup` -
  which now runs FIRST, because everything after it is `.mega`'s;
* `mega_ivt_sync`'s body (its far door keeps the real-mode refusal);
* W3's splits: `mega_svc`'s protected-mode front and back halves and
  `mega_dosgo`'s preparation, and `.cold`'s `mega_hibnc`, `mega_rmaskf` and the
  pool compactor.

**Still in `.text`, and why** - about 2.3KB of code:

* the BIOS reflection's real-mode window (`mega_svc_lo`, 198), the 286 reset
  landing (`mega_r286`), the drop into DOS (`mega_dosgo_lo`, `mega_dropr`) and
  the IRQ capture: all run in real mode after the switch;
* `mega_enter`'s real-mode refusals and its bootstrap, which runs under
  `MEGA_SEL_BCODE` before the real GDT exists, with the gate builder
  (`mega_idt_gate`, `mega_idt_build`) and `mega_irqrom` it calls, and
  `mega_coldup`, which is what makes `.mega` reachable at all;
* the routines both modes call: `mega_oddsegf`, `mega_s2segf`, `mega_slidef`,
  `mega_dsimg`, `mega_csdata`, `mega_ivt_syncf`, `mega_lincopy`;
* the doors: `mx_relallf` and `mega_dklibfree`, gated, because their callers
  reach them in real mode too, and a few `call / retf` doors for low bodies
  `.mega` calls.

**And about 1.7KB of data**: writable state, the toast templates that
`mega_tell` writes into, and the DOS park's region list and header staging
(`mega_dreg` 716, `mega_dmap` 96, `mega_dhc` 82).

### 8.4 The checks that make the split safe

`tools/os88ovlchk.py` gained four rules, and every move was made against
them:

1. `.mega` is in the near-call check: a near transfer between it and `.text`
   is refused.
2. A far transfer through `KERNEL_SEG`, `COLD_SEG` or `MEGA_SEL_CODE` must
   name a target in that segment's section. Moving code retargets dozens of
   far calls at once, and this is what found every one of them.
3. `.mega` may READ through CS and do nothing else with it.
4. `.mega`'s data is read through CS only, and a `[cs:x]` in `.mega` must
   name `.mega`'s own `x`: `mov ds, [cs:k_dsel]` is the idiom it catches.

What it cannot see is a near code pointer stored as data. There are four
kinds here, each converted by hand:

* `xm_svc`'s table, now dispatched through `mega_bpcall`;
* the IDT gates;
* the tick and keyboard chains;
* the park header's wake.

It also cannot see a STRING OFFSET handed to a routine that reads through
CS. `mm_puts` reads `.mega`'s CS, so a kernel-data line passed to it reads
`.mega`'s bytes at that offset. The DOS trip found the one case:
`mega_nmitell.say`, the tail that echoes every menu-bar line (`mega_dstr`,
`mega_nmis`, ...) to E9. Those templates are written through DS, so the tail
prints them through the low writer (`mega_putsf`). That is a review rule:
`mm_puts` takes `.mega` strings and nothing else.

### 8.5 Verified (Bochs 2.7, a 386, 16MB)

* It boots to protected mode, with `.cold` and `.mega` above 1MB.
* The string-restart probe runs. That probe is a `#GP` taken, fixed up and
  resumed entirely inside `.mega`.
* MEGAHI passes: the region above 1MB, claims, regrow, huge, file and worker.
* MEGAXM passes, including:
  * the file layer into a window above 1MB, with a packed file expanded in
    place;
  * a package's own int 13h through `mega_hibnc` and the native floppy.
* MEGACP's compaction passes.
* A sweep of the 43 shipped packages (W2's state, before the splits) started
  42 cleanly with no fault. RLAPPLY refused with `04` there, on the final
  build, and on the tree before the change alike.
* The final build passes MEGAHI, MEGAXM and MEGACP, and starts Paint, Video,
  Tracker and Sheet cleanly.
* The DOS park and return works, twice in one session. `KDHELLO.COM` is
  parked at the new pool base, `kern_mega: parked 008AK at 140000`, and the
  session comes back through `.mega`'s wake (`unparked`). The box reports
  `ended, exit code 042 (Arena: 621KB)` with every window back, and the
  console takes keys.

The refusals the sweep reports for MEGAXM and MEGACP (`04`) are the
packages' own exit codes, the same on the tree before the change.

### 8.6 Still owed

* **The Victory 286.** Bochs is a 386, so the 286 reset path - `mega_r286`,
  `mega_svc_lo`'s reset arm, `mega_dosgo_lo`'s - is the field machine's to
  prove. So is the string restart, whose 286 behaviour is what
  `mega_strfix` exists for. *86Box's mr286 has run all three since (8.7);
  the iron is still the Victory's.*
* ~~**A hibernate and resume** under Bochs.~~ Run in 8.7, with the boot
  partition on C: (`os88bochs.py --csys`): a menu hibernate and its resume,
  and KERN-MEGA-PLAN 14.43's disk image resumed after a power-off.
* ~~**W3's rest**~~: built in 8.7 - not in the park block, for a reason 3.6
  did not see.
* `.cold`'s kern_mega claim entries (`mega_claimhf`, `mega_hugef`,
  `mega_xmrunf`) stay. Their callers reach them in real mode too, and they
  are not in the 64KB segment.

### 8.7 W3's rest and W4 (2026-10-21, KERN-MEGA-PLAN 14.45)

**The park's three buffers are the TAIL OF `.mega`'s RUNG, not the park
block.** 3.6 said "build them in the park block's own header", and that
cannot be done for two of the three: the KB-in-use map (`mega_dmap`) is what
`mega_dosclaimf` SIZES the block from, before there is a block, and the list
(`mega_dreg`) is built by a hibernate's write (`hbm_xplan`) and read back by
its resume (`hbm_xres`, `hbm_xfit`) with no park block at all - the resume's
staging block is placed BY reading it. So the three live in a scratch area
past `.mega`'s last byte (`MEGA_SCR_OFF`, 16-aligned), in the same rung,
reached through their own data selector `MEGA_SEL_PSCR` = 0xFFD0 (GDT entry
1FFAh, the "optional data alias" 3.1 reserved, and one of the entries the
park already banks). `mega_mdesc` writes it beside `MEGA_SEL_CODE`, both
bases, so it is above 1MB on every machine that copies `.mega` there and in
the low rung on the rest. It is never summed (the high region stops at
`.mega`'s last whole line), which the header's cells need: they are written
after the sum. The rung grows by the 916 bytes and gives them back at the
switch, so the scratch costs no resident byte anywhere; `MEGA_PARA` and the
high-slot guard count it, and so does `tools/os88megaseg.py`.

**`mega_enter`'s refusals, the A20 gate and the task test are `.cold`**
(`mega_gatef`, 8086 code: an 8088 runs it), and the four debug-port lines
are the toasts' own text behind one `kern_mega: ` prefix rather than four
longer copies of it.

| | before (ee3c8398) | after |
|---|---:|---:|
| kern_mega `.text` + `.bss` | 54,094 | **52,716** (-1,378: the buffers 886, the refusals 492) |
| the ratchet (`tests/megaseg.txt`) | 5,935 | **4,557** |
| `.mega` | 14,252 | 14,354 (+102: segment prefixes) + 916 scratch |
| `.cold` | 41,730 | 41,994 (+264) |
| the kernel's resident rung | 54,272 | **52,736** (-1,536 conventional, every kern_mega machine) |

**W4 is built at 0x80, not 0x10.** On kern_mega `COLD_SEG` is a GDT SELECTOR
(the identity entry `mega_enter` makes code), so it must be a multiple of 8
paragraphs - mega.inc's own guard refuses anything else - and 0x10 puts it on
an odd paragraph. At 0x80 `COLD_SEG` is `COLD_RAM - 8`, the slack above task
0's stack at the top of the LOW rung, where nothing is claimed. `.cold` may
now reach 65,152 bytes before kernel.asm's FF00 guard, where 0x4000 stopped
it at 48,896. kern_big and kern_small do not move (the arm is
`%ifdef KERN_MEGA`).

**The heap, read off running machines** (the desktop after boot, A: mega.img,
`mem_top - mem_base` less the claims in `mem_tab`):

| kern_mega | Bochs 2.7, 16MB | 86Box `286-mega`, 4MB |
|---|---:|---:|
| `9eb5e828`, before `.mega` | 569,856 (floor 1360h) | 570,880 (1360h) |
| `cf7e263d`, `.mega` built | 581,120 (10A0h): **+11,264** | 582,144 (10A0h): **+11,264** |
| this change | 582,656 (1040h): +1,536 more | 583,680 (1040h): +1,536 more |
