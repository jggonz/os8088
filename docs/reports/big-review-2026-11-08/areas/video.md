# Adapter detection, geometry, mode selection, extended desktop core, fullscreen exclusive

Area key: `video`. Tree: `elendilon` at `5e4740b`. Read-only review; nothing
built or run.

## Scope read

| file | lines | read |
|---|---|---|
| `kernel/viddet.inc` | 1,903 | all |
| `kernel/vidsel.inc` | 2,299 | all |
| `kernel/vidext.inc` | 349 | all |
| `kernel/fsx.inc` | 4,105 | 1-1,470 in full (every proc on kern_big/kern_small, plus the kern_mega `fsx_rcall`, `fsx_rland` and `fsx_rtick`). Of the kern_mega real-mode bracket (1,471-4,105): the door (`fsx_rprepf`), `fsx_rlist`, the video and table parts of `fsx_rdownf`/`fsx_rupf`, and `fsx_rvdn`/`fsx_rvup`. The service-area twins (`rms_*`) were read for structure only. They are kern_mega's, and the mega reviewer owns them. |
| `kernel/extmod.inc` | 1,614 | 300-520 (`EXT_FSX`: enter, leave, unblank, kind, caps) and 1,190-1,320 (`ext_extend_b`, `ext_kapply*`, `ext_two`) |
| `kernel/vga12.inc` | - | 590-760 (`gfx_disp_run`, `gfx_disp_enter*`), 5,190-5,215 (`gfx_unlock`'s context reset) |
| `kernel/mouse.inc` | - | 366-411 (`MOU_PSTK`), 4,440-4,475 and 5,560-5,610 (the cursor's display bracket), 6,630-6,650 (`vid_rowtab` under the stack) |
| `drivers/vesa/vesa.asm` | 2,692 | 100-200, 610-720, 948-1,016 (entry, detach, attach, init, `vx_pm`, `vx_geom`, `vx_int10`) |
| `kernel/kernel.asm` | - | `VID_EXT`/`EXTCALL`/`EXT_*` definitions, kmain's video bring-up (5,690-5,760), the `fsx_*` slot rows (3,034-3,050 and 3,851) |
| SPEC.md | - | §39 (39.1, 39.2, 39.2.1, 39.11.1, 39.11.4, 39.12, 39.13, 39.18 through 39.18.3, 39.20, 39.20.1), §53 (53 through 53.10 in full), §120 (the §120.5 table) |
| plans and reports | - | `ABI-BREAK-LIST.md`, `KERNEL-BYTE-QUEUE.md`, `CODE-REVIEW-TWO-CYCLES-2026-11-01.md`, and the video rows of `LAST-DROP-BYTES.md` and `MONO-RECLAIM-PLAN.md` |
| maps | - | `build/kernel.map`, `build/smallk/kernel.map` (for symbol spans) |

## Verdict

The geometry core is the best-engineered part of this area. One
parameterized 1bpp renderer drives two banked adapters from an 18-byte
`vid_tab` row. The row table is built by iterating `gfx_nextrow`. The
16-word per-display run is copied rather than indirected. Contiguity and
adjacency are pinned by `%error` rather than by prose. The model is GEOS's
VideoDriverInfo and Mac's GDevice, done right for an 8088.

The fullscreen bracket is also sound. The kernel is parked under the app,
and there is one restore path. The caps table is the one predicate for both
greying and refusal, and every retrace wait is bounded.

The weak spots are at the edges:

- **kern_small's `fsx_page` opens the Hercules second page over a CGA.**
  This is a real bus-contention bug on the owner's class of machine. The
  dual-card guard is `%ifdef KERN_BIG`.
- **Detection trusts the first answer too much.** A plain MDA is
  programmed with Hercules 6845 timings. A PS/2 MCGA is driven as a VGA.
  A 64KB EGA gets 16 colours it cannot show.
- The VESA kind inherits a blank that hi-colour pipelines probably bypass.
- **The documentation has drifted badly in this area.** About thirty
  statements, in SPEC.md §39.11.4, §39.12, §39.13, §39.18, §53.2, §53.6,
  §53.8 and §120.5 and in in-code headers, describe retired designs or old
  slot numbers.

None of the fixes is a large byte growth. The one real size win is about
85 resident bytes of dead fsx tables on kern_small.

## Strengths

1. **One renderer, four numbers** (`viddet.inc:132-205`, SPEC §39.3).
   Hercules and CGA differ only in segment, stride, bank mask/shift and the
   wrap pair. The software renderer pointed at the framebuffer with
   `planes = 1` is the whole 1bpp driver. GEOS shipped a separate CGA and
   HGC driver. This is smaller, and a fix lands on both adapters at once.
2. **The row table is built by stepping, not computing**
   (`viddet.inc:1022-1042`). `gfx_nextrow` is the incremental form of
   `gfx_rowbase_calc`, so 348 rows cost three instructions each instead of
   a `mul`. The CGA divergence above row 206 is documented and measured
   rather than assumed away. Keep this pattern: derive a cache from the
   step form, and record where the two forms legitimately differ.
3. **Copy, do not indirect** (`vidsel.inc:1103-1111`, `vid_ctx_act`). Mac
   Color QuickDraw swapped a `GDHandle` (`SetGDevice`) and paid an
   indirection in every primitive. os8088 copies 32 bytes, about 95 µs,
   only when the display actually changes, so `gfx_nextrow` keeps its
   absolute `[cs:]` operands and its DI-only contract, which the IRQ4
   cursor path needs. This is the right trade on an 8088, and it is the
   same model as GDevice without the per-call cost.
4. **Layout invariants are assembly errors.** Examples are `viddet.inc:309`,
   `326-331`, `663-670` and `889`, and `vidsel.inc:1376-1384` and `1396`.
   The comments explain *why* each pair is adjacent, so a later edit knows
   what it breaks.
5. **The probes are written for real ISA buses.** `vid_memchk`'s
   two-values-two-offsets probe defeats the capacitive floating bus and
   also rejects a 4KB-aliasing MDA (`vidsel.inc:137-181`). `vid_cga_alias`
   distinguishes one card wearing two apertures from two cards. The
   `3BFh` retry exists for a never-configured Hercules. `[vid_hprobe]`
   makes a field report diagnosable (§39.11.1.1).
6. **Mode sets are glitch-aware.**
   - The Hercules is blanked first and unblanked last
     (`viddet.inc:1159-1178`, `1391-1413`).
   - B800 is zeroed while the CGA is still in text mode
     (`viddet.inc:1245-1263`).
   - A0000 is cleared by os8088 itself rather than trusted to the ROM
     (`viddet.inc:1296-1388`).
   - os8088 keeps its own shadow of `3D8h`, because a BIOS keeps one
     `40:65` for two cards (`vidsel.inc:1286`).

   No emulator here shows any of these defects, and each fix is cited to
   the field report that found it.
7. **The XT ROM's equipment-driven mode set is understood**
   (`vid_cga_equip`, `viddet.inc:1472-1519`). All three `int 10h` sites
   that can reach a colour card on a mono-flagged machine (`vid_setmode`,
   `vid_text`, `fsx_setbios`) go through it. This is DOS `MODE CO80`'s
   manoeuvre, applied where it is needed.
8. **The fsx bracket is a bracket** (§53.1). The kernel never runs in a
   foreign mode, and the reason is structural: it is parked on the stack.
   OS/2 1.x needed `VioSavRedrawWait` threads and screen groups to get the
   same guarantee, and Windows 3.x needed per-display grabbers. The
   restore is "ask every window what it looks like now" (`wm_paint_all`),
   which is OS/2's redraw model rather than a snapshot, and §53.6.1
   records why the snapshot was removed.
9. **One predicate for greying and refusal.** `fsx_capstab`
   (`fsx.inc:170-187`) is what `fsx_caps` answers and also what `fsx_mode`
   refuses with (`fsx.inc:600-607`). `vid_dual_ok` plays the same role for
   the Display page, `vid_disp_init` and `fsx_page`. This is §47's rule
   made mechanical.
10. **`FSXW_FRAME` waits on the PIT, not on a port, and yields**
    (`fsx.inc:887-952`). It was measured against a real consumer (Tracker's
    mixer, §53.5.1). The bounded-poll rule (`fsx_insync`'s three-tick
    timeout) means a dead status port cannot hang the machine.
11. **The extended desktop costs a one-card machine almost nothing.**
    `EXTD.DRV`'s slots rest at `mod_gone` (CF = 1, registers kept), and
    every `EXTCALL` is either behind `[vid_ndisp] > 1` or reads CF = 1 as
    the one-display answer (`kernel.asm:7091-7121`). This is the cleanest
    "feature as module" seam in the tree.
12. **The `VIDEXT` hook is minimal.** It is one `cmp [vid_planes],4` per
    planar body, with no kernel trampoline. The selector byte travels as
    the far return address and `VX_CONT` chooses the continuation
    (`vidext.inc:24-112`). `vx_null` makes a post-detach hook draw nothing
    instead of jumping into freed memory. Windows' DISPLAY.DRV gets the
    same separation by putting all of GDI's output behind a driver. This
    gets it for 12 bytes a site.
13. **Hostile `SYSTEM.CFG` bytes are bounded.** `vid_avail_test` refuses
    any kind past `VID_KIND_MAX` before the rotate, because the 8086 does
    not mask the count (`vidsel.inc:382-405`). `vid_dlay` and `vid_dmode`
    are read as "BELOW or anything else" and "zero or anything else".

## Findings

Findings are ranked by value. Each phase tag is one of:

- **now**: no ABI break.
- **flagday**: needs a break.
- **record**: worth writing down, but not recommended.

### video-1 (bug, high, now): kern_small's `fsx_page` enables the Hercules second page on a machine that has a CGA

**Evidence**

- At `fsx.inc:1027-1061` the dual-card refusal is
  `%ifdef KERN_BIG call vid_dual_ok … %endif` (lines 1031-1038).
  `vid_dual_ok` exists only inside `vidsel.inc`'s `%ifdef KERN_BIG`
  (lines 1356-2299, the routine at 1923).
- On kern_small, `BX = 0x030A` is therefore unconditional. Page 0 and
  page 1 both write `3BFh = 3`, which enables the B8000 decode, and page 1
  sets `3B8h` bit 7.
- kern_small does build `[vid_avail]` with both bits on a Hercules+CGA
  machine. The probe trio is in its `.boot2` (`vidsel.inc:114-128`).
- Its `fsx_capstab` HERC row grants `FSXM_HERC` (`fsx.inc:172`), whose
  `fsx_mtab` row advertises 2 pages (`fsx.inc:1175-1177`).

**Failure.** On the owner's class of machine (a 5150 with a Hercules and a
CGA), running the 128KB kernel, any page-flipping app does the following.
WireBall's `wbvideo.inc:82-87` is one example: it "proves page 1 is
permitted" by calling the slot.

- Two cards are put on the bus for B8000-BFFFF. This is exactly what SPEC
  §53.10 and `vid_setmode:1392-1399` exist to prevent.
- Page-1 writes land in the CGA's RAM and show on the colour monitor.
- Reads of page 1 contend between the two cards.
- CLAUDE.md's WireBall table promises "the page API refuses it there".
  On kern_small it does not.

**Recommendation.** Replace the guard with the half of `vid_dual_ok` that
applies here, on both kernels:

```
test byte [vid_avail], VID_A_VGA | VID_A_CGA
jz .single
```

The `.herc` arm runs only when `[vid_kind]` is `VID_HERC`. `vid_dual_ok`'s
EGA test and its HERC test are then trivially answered, so the colour bit
is the whole question.

**Bytes:** +2 resident on kern_big (`test m8,i8`/`jz` replace
`call`/`jc`); +11 resident on kern_small (the test, `or al,al`/`jnz` and
`mov bh,1`, which kern_small does not have today).

### video-2 (hazard, high, now): a plain MDA primary is programmed with Hercules graphics timings

**Evidence**

- `vid_detect`'s equipment-word arm makes every mono-flagged machine
  `VID_HERC` (`viddet.inc:588-594`). The "documented scope cut" at
  `viddet.inc:527-530` and SPEC §39.1 accepts this.
- `vid_setmode .herc` then writes `vid_6845`, which is R0 = 35h … the
  720x348 table at `viddet.inc:397-398`. It also writes `3B8h = 0x02`,
  then `0x0A`.

**Failure.** A real MDA has no graphics mode. It ignores `3BFh` and
`3B8h` bit 1 and keeps 9-dot text clocking. The CRTC therefore runs
R0 = 54 character clocks at 16.257 MHz / 9, about 33 kHz horizontal. An
IBM 5151 is a fixed 18.4 kHz monitor. At best this is a dead screen. At
worst it is the out-of-spec horizontal drive that period MDA documentation
warns can damage the monitor (see Open questions).

The commonest dual-monitor machine of the era is an MDA plus a CGA with
the switches set to mono. That machine is driven this way while its CGA,
which os8088 could use, sits idle.

**Recommendation.** The discriminator already exists. `vid_memchk`'s
0x1000 alias test, after writing `3BFh = 1`, is the
Hercules-versus-MDA test (`vidsel.inc:145-152`, `231-253`). Run it in
`vid_detect`'s mono arm before choosing `VID_HERC`. The text page is
visible at that moment, so two characters flicker once. The industry
alternative is Wilton's 3BAh bit 7 toggle loop (~20 bytes,
non-destructive).

On an MDA:

1. Probe B800. If it answers, choose `VID_CGA`. `vid_setmode`'s
   `vid_cga_equip` already reaches a colour card on a mono-flagged
   machine.
2. Otherwise there is no graphics adapter. Keep `VID_HERC` (today's
   behaviour), or print and halt.

Remove or rewrite the scope-cut text at the same time.

**Bytes:** about +25 to +35 of `.boot2` (transient, given back at
`mem_unblob`), 0 resident. `vid_memchk` sits in `.ovl` on kern_big and in
`.boot2` on kern_small. Check that `.ovl` is aboard at the splash's first
tick, or move the 25-byte probe beside `vid_detect`. kern_big's blob had
about 4 bytes free after kernel size pass 18's sweep3, so this may cost a
blob sector (512 bytes of disk read, not residency).

**Prior art.** GEOS, Microsoft's adapter ID and Wilton's `VideoID` all
used the 3BAh toggle precisely because an MDA must never be handed HGC
timings.

### video-3 (bug, medium, now): PS/2 MCGA is classed `VID_VGA`; the DCC's display code is never read

**Evidence**

- `vid_detect` tests only `AL == 1Ah` after `int 10h AX=1A00h`
  (`viddet.inc:554-557`).
- The header and SPEC §39.1 both name "VGA and MCGA" as answering it,
  then treat the answer as VGA.

**Failure.** The DCC returns `BL` = 0Ah/0Bh/0Ch for an MCGA (PS/2 Models
25 and 30, which are 8086 machines and squarely the target class). MCGA
has no mode 12h and no Graphics Controller or Sequencer. The result:

- `int 10h AX=0012h` fails or sets something else.
- The five GC/SEQ writes and the planar renderer then drive registers
  that do not exist.
- The screen is garbage.

Similarly, `BL = 04h`/`05h` is a VGA-BIOS machine whose active display is
an EGA, which also has no mode 12h.

**Recommendation.** After `cmp al,1Ah`:

- `BL` in {07h, 08h} → `.vga`.
- `BL` in {04h} → `VID_EGA`.
- Anything else → `.equip`. An MCGA's equipment word says colour, so it
  becomes `VID_CGA` in mode 6, which an MCGA emulates exactly.

MCGA's mode 11h (640x480x1, A000, linear, stride 80) would fit `vid_tab`
as a sixth row driven by the existing 1bpp renderer (bmask 0, wrap 0).
That is a feature, not a fix.

**Bytes:** about +8 to +12 `.boot2` (transient), 0 resident.

### video-4 (weakness, medium, now): a 64KB EGA is "driven as if it had 128KB" when `AH=12h` has already said otherwise

**Evidence.** SPEC §39.1's third scope cut: on a 64KB EGA, "the sixteen
colours collapse toward black". `vid_detect` has `BL` = memory size
(0 = 64KB) in hand at `viddet.inc:558-562` and reads only `BH`.

**Recommendation.** `or bl,bl / jz .equip`. A 64KB EGA does mode 6
perfectly, so it becomes a CGA machine, which is degraded but correct.
Delete the scope cut.

**Bytes:** +4 `.boot2` (transient).

### video-5 (bug, medium, now; PLAUSIBLE): `fsx_page` on Hercules writes the page bit and then waits, so the flip frame tears

**Evidence**

- `fsx.inc:1054-1061`: `out 3BFh` and `out 3B8h`, then a tail jump into
  `fsx_wait.vsync`.
- The header (`fsx.inc:981-984`), SPEC §53.10 and `os88api.inc`'s
  `OSAPI_FSX_PAGE` all say "the write is latched at the next vertical
  retrace".

**Why it is wrong for Hercules.** Mode X moves the CRTC start address,
which the 6845/VGA latch at frame start. Hercules' `3B8h` bit 7 is not a
6845 register. It is the memory-address MSB of the video fetch and acts
on the next character fetch. HGC programming notes say to switch pages in
vertical retrace for exactly that reason.

**Failure.** The frame scanning during the call shows its top from one
page and its bottom from the other. The contract after return still holds.

**Recommendation.** On the Hercules arm, run the two retrace phases first,
then the two `out`s. The switch is immediate, so it is on glass when the
routine returns, inside blanking. Verify on MartyPC with `m.flicker()`
across a WireBall flip (see Open questions).

**Bytes:** about +8 resident on both kernels.

### video-6 (hazard, medium-low, now; PLAUSIBLE): the planar blank on `VID_VESA` uses AC 12h, which VBE packed and direct-colour pipelines do not consult

**Evidence**

- `vid_bk .planar` (`vidsel.inc:1254-1279`) treats kind 4 as planar,
  through the `dec al / cmp al,2` map (`vidsel.inc:949-954`).
- It blanks by zeroing Color Plane Enable.
- `vid_ac_pal`'s note (`vidsel.inc:1325-1331`) retired the VGA's SR01
  bit 5 ("the second mechanism for an identical result").

**Why the result is not identical.** Color Plane Enable masks the 4-bit
attribute path. In 8bpp VBE modes on many SVGAs, and in every 16bpp mode
behind a HiColor DAC, the pixel bypasses the Attribute Controller. QEMU's
VGA applies `ar[0x12]` only in its 4bpp/2bpp line drawers. SR01 bit 5
(Screen Off) gates the signal on every VGA-class card in every mode.

**Failure.** SPEC §64's idle blanker, and the `vid_blank` darking, may do
nothing on a VESA desktop. SPEC §120.5's row ("the colour arms; a VESA
primary is a VGA to them") was never measured.

**Recommendation.** Give kinds VGA and VESA SR01 bit 5 by read-modify-write
(both are VGAs, and SR01 is readable). Keep AC 12h for the EGA alone,
whose SR01 has no Screen Off bit and is write-only. Alternatively, add a
`VX_BLANK` driver selector (VBE `4F10h`, DPMS) and keep the kernel arm.
Test on QEMU under VESA first. QEMU is exactly where AC 12h is predicted
not to work.

**Bytes:** about +12 to +14 resident on kern_big, or about +6 resident
plus driver bytes for the selector form.

### video-7 (size, medium, now): kern_small carries about 85 resident bytes of fsx tables and code for modes it can never set

**Evidence** (from the kern_small build's map, `build/smallk/kernel.map`)

- On kern_small `[vid_kind]` is only `VID_HERC` or `VID_CGA` (SPEC
  §39.27.1). `fsx_capstab`'s HERC and CGA rows grant ids 0-4 only
  (`fsx.inc:172-173`).
- Yet `fsx_mtab` assembles all nine rows. That is 126 bytes at
  `89DE..8A5C`, of which rows 5-8 (VGA0D, VGA13, VGA12, MODEX) are 56
  unreachable bytes.
- `fsx_bmodes` assembles ids 5-8: 4 bytes.
- `fsx_capstab` keeps the VGA row (2 bytes) that `vid_tab` itself drops on
  kern_small (`viddet.inc:198-203`).
- `fsx_page`'s Mode X arm (`fsx.inc:1011-1026`, about 25 bytes) can never
  run, because `[fsx_cur]` cannot be `FSXM_MODEX` there.

**Recommendation**

- Gate rows 5-8 and the four `fsx_bmodes` bytes on `GFX_VGA`. They are at
  the tail, so no index moves.
- Index `fsx_capstab` from `$ - 2` on a no-VGA build. This is `vid_tab`'s
  own idiom.
- Make the Mode X arm of `fsx_page` `%ifdef GFX_VGA`. Without it, the
  `FSXM_HERC` test's fall-through becomes the refusal.

`fsx_bmodes`' own comment argues for keeping id 8's byte to avoid "a wrong
BIOS mode" as the failure. With the mtab rows and the caps bits both
absent, nothing can index it, and the argument still holds if the
`xlatb`'s `.modex` dispatch is gated with them.

**Bytes:** about −85 resident on kern_small; 0 on kern_big.

### video-8 (hazard, low, now): `vid_apply` builds and arms the row table on `VID_VESA`, where the mouse stack is allowed to overwrite it

**Evidence**

- `mouse.inc:6630-6645` and `371-388`: under `VID_EXT`, `vid_rowtab` lies
  directly under `mou_pstack`. A packed display's arrow "may run on into
  those 696 bytes", on the argument that nothing reads a row there.
- But `VID_APPLY` (`viddet.inc:1022-1042`) still walks the VESA row
  (rowadd = BytesPerScanLine, wrap 0) into the table and sets
  `[vid_rowmax] = 348`.
- The table is therefore armed while the stack may be writing into it.
  Any unhooked caller of `gfx_rowbase` on VESA now, or a new one later,
  reads a corrupted row rather than computing one.

**Recommendation.** In `VID_APPLY`, skip the build and leave `vid_rowmax`
0 when `[vid_planes] > 4`
(`cmp byte [vid_planes],4 / ja .homed-ish` before `.rowtab`). This makes
"a packed display reads no row" structural, and saves about 4.6 ms of
pointless build per switch.

**Bytes:** +7 in each of the three `VID_APPLY` expansions (blob, CTRL.DRV,
EXTD.DRV), so +7 `.boot2` and +14 module; 0 resident.

### video-9 (bug, low, now): a same-mode bracket on the secondary polls the primary's retrace port

**Evidence**

- `fsx_wait .vsync` picks 3BAh versus 3DAh from `[vid_kind]`
  (`fsx.inc:953-958`).
- A same-mode bracket does not collapse the desktop (SPEC §39.18.3,
  `fsx.inc:760-785`), so `[vid_kind]` stays the primary's.

**Failure.** On a VGA+Hercules desktop, a same-mode bracket on the
Hercules paces to the VGA's 60 Hz retrace, and any raster-synced drawing
tears on the 50 Hz card. `fsx_surf` already answers this bracket's
display correctly from `[fsx_wdisp]`.

**Recommendation.** On kern_big, take the kind with
`mov al,[fsx_wdisp] / call vid_kind_of`. After a collapse,
`[fsx_wdisp]`'s record is the bracket display's kind too, and `[vid_kind]`
agrees, so one form serves both.

**Bytes:** about +8 resident on kern_big.

### video-10 (bug, low, now): `vid_reboot` leaves a mono SECONDARY in frozen graphics, against SPEC §39.20.1's promise

**Evidence**

- `vid_reboot` (`vidsel.inc:499-522`) handles exactly one pairing: a
  Hercules primary with a VGA, by giving the VGA mode 3 after
  `vid_text`.
- SPEC §39.20.1 promises "both monitors in a sane state — a clean text
  page on the mono card instead of a frozen graphics desktop".

**Failure.** With a VGA primary and Extend onto a Hercules (the pairing
the period built most, per DUAL-DISPLAY-VGA), `vid_text` gives the VGA
mode 3 and returns at `jne .out`. The Hercules keeps showing the frozen
desktop across `int 19h`. Hercules+CGA with either primary does the same
to the other card.

**Recommendation.** When the mono card is present and is not the primary,
set `[vid_kind] = VID_HERC` and call `vid_text` again. Its VGA-present arm
programs the mono card by hand, and its no-VGA arm uses the ROM's mode 7.
Reboot never comes back, so clobbering the kind is free. Confirm that
`HIBER.DRV`'s `hbm_bye`/`hbm_down` do not return to the GUI. A CGA
secondary can be darked with `vid_blank_kind` instead (6 bytes).

**Bytes:** about +12 resident on kern_big.

### video-11 (hazard, low, record): a mode-setting bracket's collapse leaves display 0's record describing the primary while the live block is the bracket's

**Evidence.** `vid_fsx_enter` (`extmod.inc:437-466`) does `ext_kapply`.
That republishes the live block with the bracket display's geometry, sets
`[vid_ndisp] = 1` and `[vid_cur] = 0`, and deliberately captures nothing
("the records were never written inside the bracket").
`vid_disp_find`/`vid_ctx_rect` at ndisp = 1 then read record 0: the
primary's rect and kind, not the live display's.

**Why nothing breaks today.** Nothing draws in a foreign mode. The cursor
ISR does not draw under the held lock. `mou_clamp`'s one-display path
uses `vid_wm1`. `gfx_unlock`'s `vid_ctx_act(0)` is a no-op because
`vid_cur` is 0.

**Risk.** It is an invariant held by prose. A `vid_ctx_act(0)` reached
with `[vid_cur]` = 1 inside the bracket would copy the primary's geometry
back mid-bracket.

**Recommendation.** Record it in §39.18.3 as a load-bearing rule
("between `EXTF_ENTER` and `EXTF_LEAVE` the records are the desktop's,
not the machine's"), or capture into record 0 and restore it from
`[fsx_vkind]` at leave. That costs about 10 module bytes.

**Bytes:** 0, or about +10 `EXTD.DRV`.

### video-12 (weakness, low, now): `FSI_PAGES` advertises pages `fsx_page` will refuse

**Evidence**

- `FSXM_VGA0D` says 8 pages and `fsx_page` refuses mode 0Dh
  (`fsx.inc:1008-1012`).
- `FSXM_HERC` says 2, and on a dual-card machine page 1 refuses.
- CLAUDE.md has to teach "an advertised `FSI_PAGES` count alone does not
  grant access to a second page: check `OSAPI_FSX_PAGE`'s carry".

The hardware number is honest, because a bracket may program the CRTC
itself. The *grant* is not published.

**Recommendation.** Add `FSIF_FLIP` (`FSI_FLAGS` bit 3), set at fill time
when `fsx_page` will flip this mode on this machine. Following §20.2.0.1
this is a contract addition, with no `PKG_FMT` bump. Apps then grey or
choose a path from a fact (§47) without a probing call.

**Bytes:** about +8 resident on kern_big; about +4 on kern_small.

### video-13 (weakness, low, now): the EGA row offers no planar fullscreen mode

**Evidence.** `fsx_capstab` gives the EGA row `0x000F`, the CGA modes
only (`fsx.inc:174-180`). SPEC §53.4 calls mode 0Dh "a safe later
addition rather than pass 1". Every EGA has mode 0Dh (320x200x16, 8000
bytes a plane, even on a 64KB card), and its `fsx_mtab` row already
exists.

**Recommendation.** Make the row `0x002F`, which adds id 5. Gate it on an
86Box EGA run of one mode 0Dh consumer.

**Bytes:** 0. A constant changes.

### video-14 (consistency, low, now): the kind-class idiom is spelled by hand at six sites

**Evidence.** `dec al / cmp al,2` ("planar = kind−1 ≥ 2") appears at:

- `viddet.inc:690-692`, `1306-1309`
- `vidsel.inc:1201-1204`, `1813-1815`
- `thm_set`

"Is it the mono card" is spelled `cmp …, VID_HERC` at:

- `vidsel.inc:969-972`, `1199`
- `fsx.inc:955`
- `viddet.inc:1837`

The `VID_KIND_OK` `%error` (`vidsel.inc:944-957`) is the only thing tying
them together, and it fires only when `VID_KIND_MAX` moves. The real
model is the PC's: there are exactly two display I/O ranges, 3Bx (the
mono card) and 3Dx (the colour card, in any of four kinds). That is why
the closed set is right, but nothing in the source *names* it.

**Recommendation.** Two zero-byte macros:

- `VIDK_PLANAR reg` → `dec reg / cmp reg,2`, with flags as today.
- `VIDK_MONOCARD` → `cmp …, VID_HERC`.

Use them at every site, so a sixth kind's author greps one name. Keep the
`%error`.

**Bytes:** 0.

### video-15 (weakness, medium, now): this area's comments narrate history, and roughly thirty statements have gone stale

**Evidence.** The doc/code mismatch list below: about 15 in-code and about
15 SPEC.md. Typical shapes:

- A header describing the design before a fix: `vid_disp_init`'s
  "HERC|CGA EXACTLY", SPEC §39.11.4's "writes 0 to 3B8h".
- Old slot numbers: `fsx.inc`'s 0x02C0, 0x02D0, 0x02D8 and 0x04E8.
- Step logs still in the binding SPEC: §39.12's "Nothing on a drawing path
  calls any of it yet", §39.13's "What is still owed".
- Counts: "36 bytes", "nineteen", "eighteen-word".

The files are about 70% comment, and the "it used to …" paragraphs are
where the drift collects.

**Recommendation.** Fix the list below. As a convention, a code header
states the current contract and cites the SPEC section for history. SPEC
step-log subsections (§39.12, §39.13) get a one-line "superseded by
§39.19 / §39.18.3" banner rather than silent coexistence with the
sections that replaced them.

**Bytes:** 0.

### video-16 (weakness, low, record): four macro families expand the same body into three or four images

**Evidence**

- `VID_APPLY` (blob, CTRL.DRV, EXTD.DRV)
- `VSW_EQUIP` and `VSW_UNION` (three images each)
- `VID_CTXCAP` (`ctrl`, `ext`, `cfx`/`bvs`, and written into
  `vid_ctx_init`)
- `VSW_SWITCH` and `VSW_DISP` (two images each)

Each copy reaches resident code through its own hop (`%2`: `KSWK`,
`BSWK`, `EXK`, `BSPW`). This is the right byte answer: no resident copy,
and the kernel size pass 13-15 figures are real. The cost is that a hop
bug lives in one image's expansion and is exercised only by that image's
caller path. For example, the blob's `bvs_disp_init` runs only at boot.

**Recommendation.** No change. Make sure the suite's display rows drive
each image's path once (boot, the Display page, an fsx leave on an
extended desktop) rather than only one of them.

**Bytes:** 0.

### video-17 (rethink, medium, record): the mono renderer is resident on VGA machines that will never draw 1bpp

**Evidence**

- COLD_SLIDE already gives a mono machine back the VGA rung
  (`kernel.asm:5749-5760`). That is GEOS's "load only your adapter's
  driver" applied in one direction.
- The other direction is not done. A VGA-only machine (no HERC bit)
  carries `softgfx.inc`'s `sw_*` block (`34CE..3922`, 1,108 bytes in the
  kern_big map) plus the mono arms of `font_char_bb`/`ico`/`cur`.
- It needs them only if the user picks the CGA row on the Display page
  (CTRL.DRV, a system-disk action) or if Extend arrives, which a VGA-only
  machine cannot do.

kern_big's resident budget is measured on VGA, so this is the budgeted
figure.

**Why not now.** A mono machine is the XT target and must keep the
renderer near and resident. A per-primitive far hop costs 47 µs against a
756 µs floor. The slide mechanics (two rungs, the `.cold` move) are
substantial.

**Recommendation.** Record. If KERN_BUDGET pressure returns, the shape is
COLD_SLIDE in reverse: a mono rung that a planar-only, HERC-less machine
slides over at boot, with CTRL.DRV's CGA switch refused (greyed, §47) on
such a machine rather than loading the renderer back.

**Bytes:** about −1,100 to −1,500 resident on VGA kern_big at the desktop
(ESTIMATED from the map span). Code cost not estimated.

### video-18 (rethink, low, record): no escape from a hung bracket

**Evidence.** SPEC §53.1: "An app that hangs in the bracket hangs the
machine — documented rather than defended."

**Prior art.** OS/2 1.x could always Ctrl-Esc out of a full-screen
session. Windows 3.1 added the local reboot for exactly this. Both relied
on virtualization the 8088 does not have.

**Recommendation.** Record the cooperative version. A Ctrl+Alt+Esc
latch in the keyboard ISR, `[fsx_abort]`, makes every `fsx_wait` and
`fsx_page` return CF = 1 until the bracket ends, so a well-behaved loop
exits on its next frame. It does not rescue a spinning app, which only
unwinding could, and unwinding is unsafe here.

**Bytes:** about +15 resident.

### video-19 (rethink, low, flagday): "which display" has three spellings across the video slots

**Evidence**

- `OSAPI_VIDEO` answers the primary (§39.2.1.1).
- `OSAPI_FSX_CAPS` answers the window in BX, and an in-band overload
  makes BX = 1..15 a display index (`fsx.inc:127-137`).
- `OSAPI_FSX_RUN` takes the display in CH (`fsx.inc:229-230`).
- `OSAPI_FSX_SURF` answers `[fsx_wdisp]`.

**Prior art.** Windows' `GetDeviceCaps(hdc, …)` and Mac's `GetGDevice` /
`GetMaxDevice` take one explicit device handle everywhere.

**Recommendation.** At the next flag day, unify on "display index in one
register (CH), 0 = the caller's window's", for `OSAPI_FSX_CAPS` and any
display query, and retire the 1..15 pointer overload. Not worth a break on
its own. Batch it with ABI-BREAK-LIST's T1 renumbering.

**Bytes:** about −6 resident (the range-test arms in `fsx_caps`), and
`EXTF_CAPS` simplifies.

### video-20 (prior-art, record): detection could adopt the full Display Combination Code

**Detail.** The DCC returns BH = the *inactive* display (01h is an MDA or
Hercules beside a VGA). That is evidence of a second card with no memory
write, and it is what Wilton's `VideoID` and OS/2's `VioGetConfig`
(`adapter` plus `display` plus `memory`) were built on. os8088's memory
probes work and are better tested on period cards (the PVGA1A+GB101 field
case). Recorded so a later author does not replace them with BH alone:
BH is only available on a VGA BIOS.

**Bytes:** 0.

## Rethinks

These are summarised from video-17, video-18 and video-19. None is
recommended for now.

- **A per-adapter renderer residency model** (GEOS, Windows DISPLAY.DRV).
  os8088 has the hook (`VIDEXT`) and half of the slide (COLD_SLIDE). The
  remaining half is the mono renderer on planar-only machines. It is
  worth about 1.1-1.5KB of the budgeted resident figure, and it is
  expensive to build.
- **One display handle across the API** (Mac GDevice, Windows HDC).
  This is a flag-day tidy-up, not a capability.
- **A cooperative bracket abort** (OS/2 session switch, Win 3.1 local
  reboot).

What os8088 already does better than its peers:

- Live adapter switching without a restart. GEOS and Windows 3.x needed
  setup and a restart.
- The extended desktop on two 1983 cards. Mac II-class multi-monitor did
  this on NuBus. os8088 does it on ISA with a parked-kernel fullscreen on
  top.
- The bracket's "no virtualization needed" restore.

## Prior art

| system | the model | better than os8088 | os8088 better | adopt cheaply |
|---|---|---|---|---|
| DOS + BIOS | `int 10h` owns the mode. `MODE CO80` flips 40:10 to reach the colour card. Apps own the hardware outright. | nothing systemic | os8088 forbids mode sets outside `fsx_mode`, so it always knows what to restore from (§53.7) | already adopted (`vid_cga_equip`) |
| Wilton `VideoID`, GEOS, OS/2 `VioGetConfig` | DCC BL *and* BH; EGA 12h BL/BH/CL; CRTC readback probes; the 3BAh toggle for HGC versus MDA | MDA and MCGA identification | the memory probe also defeats the floating bus and catches a never-configured HGC | video-2, video-3, video-4 |
| Windows 3.x | DISPLAY.DRV holds all output. GDIINFO describes it. Picked at setup, with no runtime switch. | one capability block per device | live switch; no per-primitive indirection on the 8088 | video-19 at a flag day |
| Mac System 5-7 (Color QuickDraw) | the GDevice list, `SetGDevice`, `DeviceLoop` per device, the main device carrying the menu bar, Monitors cdev placement | arbitrary placement; per-device depth callbacks (`DeviceLoop`) for depth-specific art | copy instead of indirect (no per-primitive cost); the dead zone costs nothing (`gfx_disp_run`) | none needed |
| OS/2 1.x | full-screen sessions with save/redraw threads (`VioSavRedrawWait`) and a screen lock | escape from a hung session | no redraw thread or VRAM save; the kernel is parked | video-18 (cooperative) |
| GEOS | one adapter driver loaded at boot; the kernel calls it through a strategy table | only one renderer resident | runtime switch; banked 1bpp sharing one renderer | video-17 |
| AmigaOS | screens with independent modes, the OS running beside them | the system keeps running | n/a on an 8088 | none |

## Doc/code mismatches

Each entry gives the file and line, the current text, what the code does,
and the proposed replacement.

1. **`kernel/vidsel.inc:2001`**, "TWO DISPLAYS IS [vid_avail] == HERC|CGA
   EXACTLY".
   - **Code:** `vid_dual_ok` (`vidsel.inc:1923-1938`) accepts any HERC bit
     plus a VGA or CGA bit, so VGA|HERC|CGA extends.
   - **Proposed:** "TWO DISPLAYS IS vid_dual_ok's answer: the HERC bit plus
     a colour bit (VGA or CGA), with an EGA primary refused. See its header
     for why VGA|CGA alone is one card."
2. **`kernel/vidsel.inc:2006`**, "VGA|HERC|CGA genuinely IS".
   - **Code:** taken, as above.
   - **Proposed:** "VGA|HERC|CGA is two cards and IS taken (vid_dual_ok,
     vid_disp_other names the VGA); a third, real CGA is never programmed."
3. **`kernel/vidsel.inc:1109`**, "36 bytes of rep movsw".
   - **Code:** `VID_CTX_W` is 16 words (`vidsel.inc:1362`), 32 bytes.
   - **Proposed:** "32 bytes of rep movsw"
4. **`kernel/vidsel.inc:388`**, "CF = 1 for AL < 4, i.e. IN range".
   - **Code:** under `VID_EXT` the bound is `VID_KIND_MAX + 1` = 5
     (`viddet.inc:119-123`).
   - **Proposed:** "CF = 1 for AL <= VID_KIND_MAX (4, or 3 without
     VID_EXT), i.e. IN range"
5. **`kernel/vidsel.inc:401`**, "CL is 1..4, never 0".
   - **Code:** CL = kind + 1, up to 5 under `VID_EXT`.
   - **Proposed:** "CL is 1..VID_KIND_MAX+1, never 0"
6. **`kernel/vidsel.inc:100`**, "(kmain:4211)".
   - **Code:** the call is `OVBCALL vid_probe_avail` at
     `kernel.asm:5727`.
   - **Proposed:** "(kmain's OVBCALL vid_probe_avail)"
7. **`kernel/vidsel.inc:1655`**, "kmain:4203 is the only edge".
   - **Code:** `OVLCALL vid_ctx_init` at `kernel.asm:5719`.
   - **Proposed:** "kmain's OVLCALL vid_ctx_init is the only edge"
8. **`kernel/vidsel.inc:326`**, "end at 0x7796".
   - **Code:** bank 3 is at 0x6000, and 87 × 90 = 0x1E96, so the end is
     0x7E96. `viddet.inc:1397` says "max offset 7E96h".
   - **Proposed:** "end at 0x7E96"
9. **`kernel/vidsel.inc:2045-2046`**, "(it is three compares, a clc and an
   stc)".
   - **Code:** a `cmp`, two `test`s, a `ret` and an `stc`, with no `clc`
     (`vidsel.inc:1923-1938`).
   - **Proposed:** "(it is a compare, two tests and an stc - and touches no
     register)"
10. **`kernel/vidsel.inc:2232`**, "the drive column, the pull-down cap, and
    39.16's own".
    - **Code:** `vid_apply` no longer computes `[vid_popmax]`
      (`viddet.inc:918-927`).
    - **Proposed:** "the drive column and 39.16's own vid_pw/vid_ph pair"
11. **`kernel/vidsel.inc:2239-2240`**, "every other display is placed to
    its right".
    - **Code:** `VID_L_BELOW` places display 1 at (0, primary height)
      (`extmod.inc:1240-1246`).
    - **Proposed:** "the other display is placed to its right or below it"
12. **`kernel/vidsel.inc:1634-1636`**, "PARITY PAD, never run, kern_small
    only".
    - **Code:** the `%ifdef KERN_SMALL nop` is inside the `%ifdef KERN_BIG`
      block that runs from 1356 to 2299, so it is never assembled on any
      kernel.
    - **Proposed:** delete the three lines, or move them outside the block
      if kern_small's parity still wants the byte.
13. **`kernel/viddet.inc:1734-1735`**, "BP naming the near `.text` routine
    - vid_apply, vid_setmode or".
    - **Code:** `vid_apply` has no resident copy (`viddet.inc:732-742`).
      `spw_near`'s BP targets are `vid_setmode`, `dock_geom`,
      `vid_depth_set` (BSPW), `font_run_x`, `vid_ctx_ptr` and
      `gfx_rowbase`. Line 1737 also reads "in `.boot2` and `.boot2`".
    - **Proposed:** "BP naming the near `.text` routine (vid_setmode,
      vid_depth_set, dock_geom, gfx_rowbase, font_run_x, vid_ctx_ptr) ...
      the loader's side pays the BP load, in `.boot2`"
14. **`kernel/viddet.inc:725`**, "1 or 4, so the sign bit is clear".
    - **Code:** on `VID_VESA` AH is the depth, 8 or 16 (`viddet.inc:681`).
    - **Proposed:** "1, 4, 8 or 16, so the sign bit is clear"
15. **`kernel/fsx.inc:107`**, "(SPEC.md 53.8, 0x02C0)".
    - **Code:** `OSAPI_FSX_CAPS` is `KERNEL_SEG:0x022F`
      (`apps/os88api.inc:5301`, `kernel.asm:3034`).
    - **Proposed:** "(SPEC.md 53.8, 0x022F)"
16. **`kernel/fsx.inc:576`**, "(SPEC.md 53.4, 0x02D0)".
    - **Code:** `OSAPI_FSX_MODE` is 0x023B (`os88api.inc:5393`).
    - **Proposed:** "(SPEC.md 53.4, 0x023B)"
17. **`kernel/fsx.inc:869`**, "the frame clock, and the present (SPEC.md
    53.5, 0x02D8)".
    - **Code:** 0x0241 (`os88api.inc:5401`). §53.5 withdrew "the present":
      nothing is flushed.
    - **Proposed:** "fsx_wait - the frame clock (SPEC.md 53.5, 0x0241)"
18. **`kernel/fsx.inc:976-977`**, "(SPEC.md 53.10, the stale slot number 04E8h)".
    - **Code:** `OSAPI_FSX_PAGE` is 0x03C5 (`os88api.inc:5414`).
    - **Proposed:** "(SPEC.md 53.10, slot 0x037B)"
19. **`kernel/fsx.inc:193-194`**, "bit 1 = FSXF_FASTTICK; others refuse)".
    - **Code:** bit 2 is `FSXF_RATE` with DX/DI (kern_big), bit 3 is
      `FSXF_REAL`, and CH is the display (`fsx.inc:221-233`).
    - **Proposed:** "CX = flags (bit 0 FSXF_KEEPWORKER, bit 1 FSXF_FASTTICK,
      bit 2 FSXF_RATE with DX/DI on kern_big, bit 3 FSXF_REAL; other CL bits
      refuse) and CH = the display (0 the window's, n display n-1,
      kern_big)"
20. **`kernel/fsx.inc:562`**, "gfx_unlock then flushes (if buffered) and
    re-shows the cursor".
    - **Code:** there is no back buffer (§53.5, §32), and `gfx_unlock`
      flushes nothing.
    - **Proposed:** "gfx_unlock then re-shows the cursor"
21. **`kernel/fsx.inc:1232`**, "and read only by".
    - **Code:** `[fsx_wdisp]` is also read by `fsx_surf` (`fsx.inc:807`)
      and `EXTF_KIND` (`extmod.inc:350`).
    - **Proposed:** "and read only by fsx_mode, fsx_surf and EXTD.DRV's
      EXTF_KIND, all inside the bracket"
22. **`kernel/kernel.asm:3049-3050`**, "frame clock / present (SPEC.md".
    - **Code:** no present. AL = 2 (`FSXW_FRAME`) is also accepted
      (`fsx.inc:920-922`).
    - **Proposed:** "frame clock (SPEC.md 53.5): AL = 0 tick / 1 retrace / 2
      the next IRQ0, yielding"
23. **`kernel/kernel.asm:3035`**, "AX = the FSXM bitmask this".
    - **Code:** it asks about the window in BX, or the display
      BX−1 for BX = 1..15 (`fsx.inc:124-152`).
    - **Proposed:** "AX = the FSXM bitmask the display of window BX (or
      display BX-1) can set, DL = its kind"
24. **`SPEC.md:76123`**, "keeps the whole table resident (`VID_TAB_RES`)
    only because".
    - **Code:** only `KERN_MEGA` defines `VID_TAB_RES`
      (`viddet.inc:165-167`). kern_big took the per-image arm at thirteen
      sectors (`viddet.inc:159-164`).
    - **Proposed:** "`kern_small`, `kern_emu` and `kern_big` each carry the
      rows in every expansion and keep only `vid_vxrow` resident (-36 / -72
      / -72); `kern_mega` alone keeps the whole table resident
      (`VID_TAB_RES`), because its switch rewrites the segment column to
      selectors in place."
25. **`SPEC.md:230763`** (§120.5's MENU_BCMAX row), "`kern_small` and
    `kern_mega` have no `VID_EXT` and keep 85".
    - **Code:** `VID_EXT` is defined on every `KERN_BIG` build including
      kern_mega (`kernel.asm:849-862`), and `vx_boot_x` has a `KERN_MEGA`
      arm (`vidext.inc:251-259`).
    - **Proposed:** "`kern_small` has no `VID_EXT` and keeps 85;
      `kern_mega` has it (§120.3.4) and takes 123"
26. **`SPEC.md:77304`**, "Leaving the Hercules writes 0 to 3B8h".
    - **Code:** `vid_blank` delegates to `vid_blank_kind`
      (`vidsel.inc:963-978`). That writes 3B8h = 0x02 only, leaves 3BFh
      alone, writes `(vid_cgamode & ~8)` to 3D8h, and blanks a VGA or EGA
      through AC 12h (`vidsel.inc:1191-1284`).
    - **Proposed:** "Darking is `vid_blank_kind`'s (§39.18.1): the
      Hercules gets 3B8h = 0x02 (graphics, page 0, video off) and 3BFh is
      left alone; a CGA gets its own banked 3D8h less bit 3; a VGA or EGA
      gets Color Plane Enable = 0."
27. **`SPEC.md:77317`**, "The one case it does not cover is a VGA".
    - **Code:** covered by `vid_bk .planar` (`vidsel.inc:1254-1279`).
    - **Proposed:** delete the paragraph, or say "A VGA and a Hercules is
      covered: the planar arm darks the VGA through the Attribute
      Controller (§39.18.1)."
28. **`SPEC.md:77031`**, "four banks of 87 × 90 end at".
    - **Code:** the last displayed byte is 0x7E96.
    - **Proposed:** "four banks of 87 × 90 end at 0x7E96"
29. **`SPEC.md:77359`**, "It is `vid_apply`'s own idiom. 36 bytes".
    - **Code:** 16 words, 32 bytes.
    - **Proposed:** "It is `vid_apply`'s own idiom. 32 bytes"
30. **`SPEC.md:77416`**, "Nothing on a drawing path calls any of it yet".
    - **Code:** `gfx_disp_run`, `gfx_disp_enter*`, the cursor bracket and
      `gfx_unlock` all call `vid_ctx_act` (`vga12.inc:677`, `742`, `5206`;
      `mouse.inc:4463`, `5581-5600`).
    - **Proposed:** "(Step 1's gate, historical.) Drawing now activates a
      display per primitive (§39.14); this paragraph records how step 1
      landed."
31. **`SPEC.md:77430`**, "Two displays means `[vid_avail]` is".
    - **Code:** `vid_dual_ok` takes HERC plus (VGA or CGA), with an EGA
      primary refused.
    - **Proposed:** "Two displays means `vid_dual_ok` (§39.19): a HERC bit
      and a colour bit. VGA|CGA alone is one card; VGA|HERC|CGA is taken
      (DUAL-DISPLAY-VGA)."
32. **`SPEC.md:77446`**, "turns a `vid_tab` row into the nineteen words".
    - **Code:** `VID_CTX_W` = 16.
    - **Proposed:** "into the sixteen words"
33. **`SPEC.md:77489`**, "What is still owed, and it is one thing".
    - **Code:** `vid_switch` re-runs `vid_disp_init` (`vidsel.inc:1038-1045`).
    - **Proposed:** "(Since discharged: `vid_switch` re-runs
      `vid_disp_init` as its step 3, §39.19.)"
34. **`SPEC.md:79231`**, "`fsx_run` picks the display with `wm_disp_of`
    (§39.17), makes it".
    - **Code:** `fsx_run` only banks `[fsx_wdisp]` (`fsx.inc:405-421`). The
      collapse is in `fsx_mode` (`fsx.inc:619-627`), per §39.18.3. The
      paragraph's "A SAME-MODE bracket still changes the geometry" is the
      retired behaviour.
    - **Proposed:** "`fsx_run` resolves the display with `wm_disp_of`
      (§39.17) and banks it; the first `fsx_mode` call makes it the only
      display at the virtual origin (§39.18.3)." Also delete or invert the
      same-mode paragraph.
35. **`SPEC.md:79241`**, "eighteen-word run".
    - **Code:** sixteen words.
    - **Proposed:** "sixteen-word run"
36. **`SPEC.md:106741`**, "Both armed and cleared under `pushf`/`cli`".
    - **Code:** both are armed and cleared by ONE word store with no
      `cli`, atomic on an 8086 (`fsx.inc:337` and `353-360`, `489-490`).
    - **Proposed:** "Both armed and cleared by one word store (adjacent,
      `[fsx_task]` first): an 8086 takes an interrupt only between
      instructions, so `sch_switch` never sees half a pair."
37. **`SPEC.md:107564`**, "3. Disarm: `[fsx_task]` = `[fsx_worker]` = 0xFF,
    under `cli`."
    - **Code:** one word store, no `cli` (`fsx.inc:489-490`).
    - **Proposed:** "3. Disarm: `[fsx_task]` = `[fsx_worker]` = 0xFF in one
      word store."
38. **`SPEC.md:107561`**, "event queue (`evq_pop` until empty)".
    - **Code:** `evq_drain` into `fsx_evtmp` (`fsx.inc:486-488`).
    - **Proposed:** "event queue (`evq_drain` into `fsx_evtmp`)"
39. **`SPEC.md:107569`**, "With `[bb_dbl]` armed the repaint renders into
    the".
    - **Code:** `[bb_dbl]` no longer exists (§53.5's own paragraph).
      Step 5's "flushes (if buffered)" has the same problem.
    - **Proposed:** delete the `[bb_dbl]` sentence, and in step 5 write
      "its dispatch epilogue's `gfx_unlock` brings the cursor back".
40. **`SPEC.md:107740`**, "fsx_caps   out AX = FSXM bitmask for the live
    adapter".
    - **Code:** it takes BX (a window, or 1..15 a display) and answers that
      display's mask and kind.
    - **Proposed:** "fsx_caps   in BX = window (0 frontmost) or 1..15 =
      display BX-1; out AX = that display's FSXM bitmask, DL = its kind
      (VGA for VESA); CF=1 no such display"
41. **`SPEC.md:107750`**, "0x0241 fsx_wait   in AL = 0 next tick / 1
    vertical retrace".
    - **Code:** AL = 2 (`FSXW_FRAME`) as well. The 53.8 table also lacks
      the 0x03C5 `fsx_page` row and `fsx_run`'s bits 2 and 3 and CH.
    - **Proposed:** add "/ 2 next IRQ0, yielding (§53.5.1)", add a row
      "0x03C5 fsx_page in AL = page; out CF (§53.10)", and give `fsx_run`
      "(bit 0 KEEPWORKER, 1 FASTTICK, 2 RATE + DX/DI, 3 REAL; CH display)".

## Open questions

1. **MDA and out-of-spec drive (video-2).** The claim that driving a 5151
   at about 33 kHz risks the monitor rests on period warnings. The IBM
   MDA technical reference cautions against improper 6845 programming.
   Someone with the document should confirm the wording. Separately, IBM
   documents MDA `3B8h` bit 0 (high resolution) as required. `vid_setmode`
   writes 0x02 and 0x0A, which have bit 0 clear. A Hercules ignores bit 0,
   and a real MDA may not.
2. **The Hercules page latch (video-5).** Does MartyPC's Hercules model
   switch `3B8h` bit 7 immediately or at frame start? If it latches, the
   WireBall frame counts in PERFORMANCE.md Set 160 cannot have seen a
   flip-frame tear either way. An 86Box HGC and `m.flicker()` across one
   flip would settle it.
3. **AC 12h on VESA (video-6).** Under QEMU with `VESA.DRV` at 8bpp and at
   16bpp, does SPEC §64's idle blank actually dark the glass?
4. **`vid_reboot`'s callers (video-10).** Do `HIBER.DRV`'s `hbm_bye` and
   `hbm_down` ever return to the GUI after `vid_reboot`? The proposed fix
   clobbers `[vid_kind]`.
5. **Blob room (video-2, video-3, video-4).** Is there room in kern_big's
   blob for about 45 transient bytes, or does it take the fourteenth
   sector? Either way it is disk, not residency.

## Verification

Adversarial pass, 2026-11-08: every bug, hazard, high and medium finding was
re-read against the code (both build arms) and the plans. No finding was
refuted. The data file `data/video.json` carries the corrected fields.

- **video-1**: CORRECTED. The bug is real: the only refusal is under
  `%ifdef KERN_BIG`, and kern_small writes 3BFh = 3 on every call, page 0
  included. kern_small's cost is +13 bytes, not +11.
- **video-2**: CONFIRMED. The fix needs its own copy of the alias test in
  `.boot2`, because on kern_big `vid_memchk` is in `.ovl`, which the splash
  cannot reach. The monitor-damage risk is period lore and is not verified
  here.
- **video-3**: CORRECTED. The MCGA defect is real. Divert only BL =
  0Ah..0Ch to `.equip`. Sending "anything but 07h/08h" there would also
  move a VGA+mono machine (BL = 01h) onto a Hercules primary.
- **video-4**: CONFIRMED.
- **video-5**: CONFIRMED in code order. The hardware claim stays
  PLAUSIBLE until it is measured.
- **video-6**: CONFIRMED in the code path. The effect on the glass stays
  PLAUSIBLE until it is run under QEMU.
- **video-7**: CORRECTED. The saving is -82..-86 bytes. fsx_bmodes'
  id-8 byte is kept on purpose, and fsx.inc:1194-1203 gives the reason.
- **video-8**: CONFIRMED.
- **video-9**: CONFIRMED. The cost is +3..+8 bytes.
- **video-10**: CORRECTED. The defect is real, but the proposed fix would
  break HIBER.DRV: `hbm_stageseg` reads `[vid_kind]` after `vid_reboot`, so
  the fix must bank `[vid_kind]` and restore it. It must also program the
  card by hand, because int 10h mode 7 is wrong on Hercules+CGA with a CGA
  primary. The cost is +18..+24 bytes.
- **video-11**: CONFIRMED.
- **video-12**: CONFIRMED (spot-checked).
- **video-13**: CONFIRMED (spot-checked).
- **video-14**: CONFIRMED (spot-checked).
- **video-15**: CONFIRMED. All 41 quotes were found verbatim, and the
  claims they make are confirmed.
- **video-16**: unchecked.
- **video-17**: CORRECTED. A VGA machine's CGA row does draw 1bpp. The
  span is confirmed at about 1,108 bytes. Cite MONO-RECLAIM-PLAN.
- **video-18**: unchecked. The SPEC quote was located.
- **video-19**: CONFIRMED (spot-checked).
- **video-20**: unchecked.

Doc mismatches. The numbers follow the list above.

- **1 and 2**: CONFIRMED, merged into one rewrite of the paragraph.
- **3 to 10**: CONFIRMED.
- **11**: CONFIRMED, but rewritten as a comment-only fix. Deleting the
  dead `%ifdef KERN_SMALL` nop would be a code change.
- **12**: CONFIRMED, refined. `spw_near` has more targets than the list
  proposed (menu_relayout, inst_vol_enter and others), so the fix names the
  splash's own targets.
- **13 to 41**: CONFIRMED. The replacements in 18 and 41 are refined
  against the real flags (FASTTICK with RATE refuses, and kern_small takes
  CH = 0 only).

Three stale neighbours were added: `vid_disp_init`'s "pull-down cap",
§39.18's "A SAME-MODE bracket still changes the geometry" and step 5 of
§53.6. The "latched at the next vertical retrace" wording for the Hercules
page was deliberately not fixed until video-5 is measured.
