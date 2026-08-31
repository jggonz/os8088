# The boot, phase by phase — what was measured, what changed, what is left

A handoff. It exists because this work is **measurement-led** and the
measurements are the expensive part: most of them come off an 86Box XT that is
not in this repository, several took a screenshot and a reply to obtain, and
two of them contradict what the code's own comments say. Losing them means
taking them again.

Everything here is on branch **`boot-status-lines`**, cut from
`elendilon-new-2`. `make test-full` green on each commit.

## 1. Where the time goes

The field machine is **`8088VGA`** — 86Box 6.0, `ibmxt86`, 8088 at 10 MHz, VGA,
`msserial` mouse, ST-225 on an ST-11M, two floppy drives unless a row says
otherwise. `make BOOTPROF=1` (§15.5) produced every column.

### 1.1 Hard-disk boot, before and after this branch

| phase | before | after |
|---|---|---|
| boot + early init | 329 ms | 329 ms |
| clock + video + heap | 202 ms | 202 ms |
| mouse_init | 619 ms | 618 ms |
| **desktop + drivers** | **1,782 ms** | **51 ms** |
| drv_boot | 734 ms | 734 ms |
| first paint | 136 ms | 146 ms |
| **TOTAL, OS only** | **3,790 ms** | **2,087 ms** |

`desktop + drivers` was **47% of the boot**. It is §18.97's floppy probe and
nothing else in that phase is measurable.

### 1.2 Floppy boots, after the change

| config | desktop + drivers | drv_boot | TOTAL |
|---|---|---|---|
| 1 drive | 25 ms | 1,972 ms | 10,547 ms |
| 2 drives, disk in B: | 38 ms | 2,153 ms | 11,371 ms |
| 2 drives, **B: empty** | 38 ms | 2,153 ms | 11,371 ms |
| 3 drives, others empty | 52 ms | 2,144 ms | 10,767 ms |

**The B:-empty row is a same-config A/B**: before the change it read
`desktop + drivers 1,782 ms`, `drv_boot 2,758 ms`, `TOTAL 13,129 ms`. Every
drive listing was correct in all five runs (A:, B:, C:, D: as configured).

### 1.2.1 …and then the splash stopped teletyping (§15.3.2)

Everything in §1.2 was measured with the caption and the percentage still going
through `int 10h` AH=0Eh. **A mode-12h BIOS character costs 40 ms** (§15.6.5.1),
`spl_chrome` drew 18 of them and `spl_pct` four *a tick*, and blitting both
instead took a 360KB floppy boot on `os8088_xt_vga` from **21,959.1 ms to
16,084.2 ms** — one tree built twice, `tools/os88boot.py`:

| phase | before | after | delta |
|---|---|---|---|
| POST | 5,817.5 | 5,817.5 | — |
| `boot: int 13h x18` | 7,581.7 | 7,196.0 | −385.7 (mechanical) |
| **`boot: sector loop`** | **3,765.9** | **993.3** | **−2,772.6** |
| `mouse_init` | 782.9 | 618.1 | −164.8 |
| `dock_init` | 243.9 | 83.4 | −160.5 |
| **`drv_boot`** | **3,293.9** | **1,063.2** | **−2,230.7** |
| `mem_unblob_x` | 213.7 | 52.9 | −160.8 |
| **TOTAL from reset** | **21,959.1** | **16,084.2** | **−5,875.0** |

A notch of the bar cost **160.2 ms** (four digits and the cursor set), so every
phase that repainted shed a flat 160 ms whether it did work or not — `dock_init`
was two thirds percentage. `drv_boot` repaints once a sector and shed fourteen.

**Where the 40 ms goes is now known to the instruction and it is not ours.**
Breaking on `int 10h` under MartyPC prices the call at 189,700 cycles / 12,030
instructions and `AH=02h` at 4,900; 316 of 317 IP samples inside one call are in
the video BIOS. It is Bochs VGABIOS's `write_gfx_char_pl4` doing an `outw` to
the Graphics Controller plus a read and a write of `A000` **per pixel**, through
C stubs that spend 9–12 instructions each. Any VGA BIOS does the same in a
planar mode; the fix was not to call it.

### 1.3 The numbers that are NOT a bug

- **`mouse_init` is fine.** §9.4.5's early close fires on every machine tested:
  `used 0008 of 0013` — `MOU_IDFLOOR` exactly. 585–619 ms with a mouse, 1,190 ms
  without. The 1.23 s originally attributed to it was the loading screen's own
  chrome plus the window (§9.4.6.2).
- **`boot + early init` is 7,855 ms on a floppy and 329 ms on a hard disk.**
  That is §18.91's transfer and it is the biggest single item left. Untouched
  by this branch.

## 2. What landed

| § | what |
|---|---|
| §2.9.9.2 | the hard-disk path set the video mode **twice**; `spl_step` enters at `spl_rechrome`. **116.8 ms**, measured on one installed partition booted each way, zero bytes |
| §15.6.4 | `Looking for Mouse` and `Looking for Drives` — the two phases a hard-disk boot spends at 0% and 6% |
| §15.6.5 | `Loading Kernel` through the floppy load, **glyph-blitted** |
| §15.6.5.1 | a mode-12h BIOS teletype character costs **40 ms** on a 4.77 MHz 8088 |
| §9.4.6 | `make MOUDIAG=1` — the identify window's state on the desktop |
| §52.10.2.1 | a knob kernel's VBR pointed `BLOB_SEG` at the **shipped** kernel's heap floor |
| §18.97.5 | the probe's motor wait moves to the only path that can remove a drive |
| §15.3.2 | the caption and the percentage are blitted too, and `spl_text` is **opaque** — **−5,875 ms of a floppy boot**, +13 bytes of `.boot2`, nothing anywhere else |
| §15.3.3 | `mouse_init`'s waits charge the bar a notch a **tick** — the hard-disk boot's worst lie **−43.8 → +8.0 points**, its longest hold **630 → 105 ms** |
| — | `os8088_xt_vga_hdd`, the first VGA + hard-disk MartyPC machine (docs/MARTYPC-DEBUG.md) |
| §15.3.4 | the spinner composes each row and stores it once — **28.7 → 10.6 ms** and the blink gone |
| §15.3.5 | **the mono adapters get the whole screen** — dialog, caption, percentage and spinner, VGA 0 differing pixels |
| §9.4.6.3 | the `MOUDIAG=1 BOOTPROF=1` refusal is **retired** — it does not reproduce, and two tests replace it: `vbrseg` (fast) and `knobhd` (soak) |

## 3. The instruments, and how to drive them

### 3.1 `make MOUDIAG=1` (§9.4.6)

Seven lines on the finished desktop. The fields that matter:

```
win open 0003  used 0008  of 0013 ticks     <- 8 = MOU_IDFLOOR = early close
row  base  idn  b0  last  idt  nd  run      <- per serial port
idany 1  port 0  seen 0  p2st 00  hpst 0
chrome 0000  mouse 000B  fdd 0020  ticks    <- kmain's notch-to-notch gaps
fdd1 st3 29 39 st0 20 step 2 vrd 1 eqp 2    <- 18.97's own verdict, UNIT 1
```

`step`: **1** fast exit, **2** `SEEKOK`, **3** absent, **4** `NORDY`,
**5** `SEEKST0`. `chrome` is meaningless on a floppy boot.

### 3.2 `make FDDSLOW=1` (§18.97.5)

Forces the probe down its slow path whatever ST3 says. **The only way to
exercise `.recal` outside the field** — both emulators answer TRK0 set
unconditionally (§18.97.2).

### 3.3 `make BOOTPROF=1` (§15.5)

The whole boot in milliseconds off the PIT. **`MOUDIAG=1 BOOTPROF=1` is
refused** — see §5.2.

### 3.4 Knob builds need `$OS88_DEFINES`

Every host tool that resolves a kernel symbol re-assembles `kernel.asm` and
refuses a map that is not byte-identical to `build/kernel.bin`. A knob kernel
never is. So:

```sh
make MOUDIAG=1
OS88_DEFINES="MOU_DIAG" python3 tests/hdboot.py
```

Forgetting it reads as *the test is broken*, not *the map is unavailable*.

## 4. Techniques that earned their keep

- **Frame-differencing a screen capture.** The user's `.mov` of a boot was
  decoded with ffmpeg and the loading screen's field measured per frame, which
  is what dated the two stalls to 1.23 s and 1.80 s and disproved the first
  attribution.
- **Sampling `m.regs()` in a loop** while the guest is wedged, mapping IP to
  the nearest `.text` symbol. That is what found the kernel executing
  `mdg_t5`'s string data. `os88marty` also has `read`/`readseg`/`vram(kind)`/
  `fbuf()`, and `os88sym.linear(name)`.
- **Reading a published block out of the guest instead of adding state.**
  §57.5's `fdd_dbg_u` answered the probe question with no kernel change at all.
- **A/B at one commit.** Every ms figure here is the same tree built twice, not
  two commits compared.

## 5. Open, in the order worth doing

### 5.1 The probe's win is NOT this branch's — what provokes the slow path is unknown

`desktop + drivers` went **1,782 → 38 ms on the same configuration**. But 38 ms
is under one tick, so the probe took the **step-1 fast exit** and never reached
the recalibrate — and §18.97.5 only moves `FDD_MOTORW` off the recalibrate path.
It cannot make step 1 succeed.

**The `step` digit came back and it reads 1.** On the 2-drives / B:-empty
floppy config, the reworked build reports:

```
chrome 0000  mouse 000B  fdd 0001  ticks
fdd1 st3 39 FF st0 FF step 1 vrd 1 eqp 2
```

`ST3 = 39` — TRK0 **set**, on an empty drive B — `ST3B = FF` never read, the
fast exit, one tick. **`.recal` and `.lastchance` did not execute**, so nothing
§18.97.5 changed ran on that boot.

So the win is NOT this branch's. What changed is what the FDC answers at step 1,
on the same machine and the same configuration, and no edit here can reach that.
What §18.97.5 *is* measured to do is remove `FDD_MOTORW` from the present path:
**40 -> 28 ticks on MartyPC under `FDDSLOW=1`, one tree built twice, -12 ticks
exactly.** That stands; the field's 1,782 -> 38 ms does not attach to it.

**The discriminating test, not yet run**: re-boot the EARLIER `BOOTPROF=1`
image on the same B:-empty config. If `desktop + drivers` now reads ~38 ms on
the old code too, the variable is machine state and neither build moved it.

Leading suspect for that state: **cold power-on vs soft reset** — whether 86Box
re-homes the emulated head at machine start but not across a reset, which would
make the 1,782 ms a first-boot-after-power-cycle cost and every later run fast
whatever is installed. Control for it by power-cycling before each run.

**The 1,782 ms is real and was measured twice; what is unknown is what provokes
it.** Until that is named, treat the probe as still costing 32 ticks whenever
ST3 comes up TRK0-clear, and §5.7's levers as still wanted.

### 5.2 `MOUDIAG=1 BOOTPROF=1` — **DONE**, the refusal is retired (§9.4.6.3)

It does not reproduce. At HEAD **and at §52.10.2.1's own commit**, with the
refusal lifted, the pair installs and boots off a hard disk on **both**
adapters and reaches a desktop with both tables drawn; `[spl_fseg]` reads the
kernel's own `HEAP_SEG` (`1DA0`) the whole time the splash is up.

What the field actually reported — *"locks on boot from hard drive with just
the boot splash up, no bar or percentage in it"* — is §52.10.2.1 exactly:
chrome drawn, blob three rungs low, bar never advancing. The later `gfx_blit4`
sighting was taken in-session and is far more likely to have been a stale
`build/` than a second defect.

**Two tests replace the guard**, which is what the tree does with a suspicion:
`tests/unit/t_vbrseg.py` (fast) reads `BLOB_SEG`/`SPL_FSEG` back out of the
assembled `boothd.bin` and compares them with `build/kernel.bin`'s map —
verified to fail when `mov dx, BLOB_SEG` is patched from `1DA0` to the shipped
`1D20` — and `tests/knobhd.py` (soak) is §5.5 below.

### 5.3 Blit the whole loading screen — **DONE**, SPEC.md §15.3.2

Both estimates held: the percentage was ~2.5 s of a VGA boot and the caption
~700 ms. §1.2.1 is the measurement. The renderer had to change with the call
sites — `spl_text` **merged** through the Bit Mask, which cannot redraw a
reading that shrinks — and opaque turned out to be one instruction a row rather
than four, so it is faster as well as correct.

**What is deliberately NOT done**: the mono splash still carries the bar and
§15.6's line alone. `spl_text` draws on all three adapters, so a caption and a
reading on 640×200 and 720×348 are now a question of *where to put them* and of
somebody looking at the result (§39.4) — a look decision, not a limit.

### 5.4 Weight the splash bar by predicted duration — **DONE**, SPEC.md §15.3.3

Measured first, and the answer was not the one filed. Sampling
`[spl_done]`/`[spl_total]` against the cycle counter says the **floppy** bar
was only ever wrong by +14.4 points, and the **hard disk** by −43.8 — 630 ms of
a 1,365 ms splash sitting at 0%, which is `mouse_init` charged one notch spent
at the end.

**A weight cannot fix a hold**, and the arithmetic says so: charging the phase
11 notches moves where the bar lands *after* the wait, not where it is
*during* it. So the wait charges a notch a tick instead, and `SPL_POST`
(16 → 27) carries the guess — `MOU_RSTLOW` + `MOU_IDFLOOR` = 11 ticks, which is
what a machine WITH a mouse spends. The guess is a **clamp**: a machine with no
mouse spends `MOU_IDWIN` and holds for the difference rather than jumping.

Worst lie −43.8 → **+8.0** points, longest hold **630 → 105 ms**; floppy
+14.4 → +10.1. Costs 47 bytes of `.text`, no rung.

**What made it decidable was a cost measurement**: `spl_paint` is **32.4 ms**
and `spl_spin` is **28.7 ms of it**. A 1200-baud 7N1 identify byte is 7.5 ms of
line time, so a full repaint inside the identify window would lose three of
them; the quiet notch (`splf_stepq`, no spinner) measures **4.81 ms** and
cannot lose one. `MOU_DIAG` reads identically before and after.

**Still open from this one**: the spinner is 88% of every repaint and about
**830 ms of a floppy boot** — see §5.8.

### 5.5 A knob kernel booted from a hard disk — **DONE**, `tests/knobhd.py`

The gap that let §52.10.2.1 survive: the build matrix **assembles** knob
kernels and never boots one, `hdboot` boots a disk and only ever the shipped
kernel, and every other boot row is a floppy. The defect needed all three at
once, because the volume boot record is the only loader that has to be TOLD
where the heap starts — `boot/boot.asm` learns it from the kernel it has just
loaded.

`knobhd` builds `BOOTPROF=1 MOUDIAG=1` (the pair that moves the ladder
furthest), installs it, boots it off the disk on `os8088_xt_hdd` **and**
`os8088_xt_vga_hdd`, and asserts `[spl_fseg]` reads `HEAP_SEG` live. It
rebuilds `build/` and puts it back (`tests/gfxlk.py`'s pattern) and erases the
VHD. ~10 minutes, which is why it is soak.

**The VGA machine is the half that was missing.** Every hard-disk config in the
tree was CGA, where the loading screen draws the bar and nothing else, so
`spl_step`'s start path and `spl_rechrome` drew no chrome to go wrong.

### 5.6 Smaller, real, not this branch's

- **`tests/bootstatus.py` expects `Loading Driver 1/2 (Ethernet)` + `2/2 (Ram
  Disk)` and only Ethernet loads.** Pre-existing — identical before this branch.
- **The probe's spin-up tax.** Before §18.97.5 the probe left the BIOS motor
  byte cleared, so the next floppy access paid a spin-up: `drv_boot` 2,758 →
  2,153 ms on a B:-empty floppy boot. Fixed as a side effect; recorded because
  it is the kind of cost that hides in a neighbouring phase.
- **86Box cannot express drive 0 + drive 2 with no drive 1** (the field 5150's
  own configuration) — it trips a BIOS 601. Anything measured after a 601 is
  downstream of a controller the BIOS has already failed.

### 5.7 Further levers on the probe, if 5.1 leaves anything

1. **Poll the recalibrate**, bounded by `FDD_SEEKW`. A present drive stops
   paying the 20 ticks; an absent one pays exactly what it does now. §18.97
   rejected a bare poll for the absent case — the bound is what makes it safe.
2. **Defer the probe off the boot path.** Its answer only decides whether a
   `Disk B` zone is shown. Show it per the equipment word, probe after the first
   paint, retire the zone if the verdict comes back absent.

## 6. A fresh container needs

`nasm`; `libudev-dev` + `pkg-config` before `make marty` (it fails on
`libudev-sys` otherwise); `qemu-system-x86` for `test-full`'s boot rows;
`ffmpeg` and `pillow` if a capture has to be read.

**Only the GLaBIOS MartyPC machines run here** — `os8088_5150_cga` wants
`ibm5150_82_v4`, which is not in the tree. Use `os8088_5150_cga_gla`,
`os8088_5150_herc_gla`, `os8088_xt_vga`, `os8088_xt_hdd` and
`os8088_xt_vga_hdd`. That last one is new (§2) and is the only way to see the
loading screen's **dialog** on a hard-disk boot; its GLaBIOS menu times out to
A: before frame 300, so press `KeyC` more than once.

### 5.8 `spl_spin` — **DONE**, SPEC.md §15.3.4: it composes rows now

**The flicker was the point, and it was not in the estimate.** The blank-then-
stroke pair *is* a double-draw at the largest scale in the tree, and with ~29
repaints half a second apart it reads as the logo blinking — reported from the
field, and visible in the measurement: one displayed frame per repaint showing
13% of the picture.

Measuring first changed the design twice.

1. **615 opaque byte writes cost 2.70 ms** (21.0 cycles a byte), so of
   `spl_spin`'s 28.7 ms the blank was 2.7 and the *stroking* was 26 — **335
   cycles a byte**, nearly all of it `spl_wrbyte` call overhead rather than the
   VGA. The cost was never the pixels.
2. **Erasing the previous frame's strokes does not work.** The angle cannot be
   derived (`spl_tick` assigns `spl_done` a sector count, so frames are ~13
   angles apart, not 1), and interleaving erase with redraw *corrupts the
   picture* — a later digit's old bar spans across an earlier digit's new
   strokes. Measured: `399 … | 352 217 | 203 203 203` where the reference
   settles at 400. Erase-all-then-draw-all is correct and empties the box
   exactly as `spl_fill` did, so it buys the speed and none of the flicker.

So the box is **composed a row at a time in RAM and written once**: 38 of 41
rows are the same row, only 0/20/40 carry a bar, two 15-byte buffers hold
everything. **28.7 → 10.6 ms**, no frame ever shows a broken picture, and all
27 settled pictures byte-identical to the reference build.

Whole boot 16,084.2 → 15,849.2 ms; the per-phase deltas are the honest figure
(~18.1 ms a spin, ~450 ms of CPU across ~25 spins, the rest absorbed by
rotational latency). Cost: `OVL_AT` 2,048 → 2,560, which is free — the blob is
13 sectors either way — but spends its last slack: `.ovl` now ends 159 bytes
short, so the next claim is `BOOT2_SECS` at a real ~24 ms a boot.

**Still on the table**: `spl_spin`'s remaining 10.6 ms is ~5 ms of `rep movsb`
to VGA and ~5 ms of composition, of which the 32 `imul`s in `spl_xform` are the
biggest part — the eight x values are the same for all four `spl_rowmk` calls
and could be computed once into a table.

### 5.9 A burst of identify bytes has not been tested against §15.3.3

MartyPC's serial mouse sends **one** byte (`M`). §9.4.1's back-to-back PnP
string is what the 7.5 ms margin is really for, and nothing here can produce
one. A field run with `MOUDIAG=1` would settle it: `idn` above 1 on the mouse's
port, with `idany 1` still set.
