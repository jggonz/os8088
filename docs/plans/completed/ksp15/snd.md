# ksp15 agent "snd" - SOUND.DRV's resident keep under 6KB

Kernel size pass 15 (`docs/plans/completed/ksp15/BRIEF.md`). Assignment:
`drivers/sound/`, `snd_keep` (what a machine with no mixer keeps resident,
SPEC.md 34.15.1) from 6,392 to under 6,144; bonus `snd_keepmix` from 9,937
toward 9,216. Branch `ksp15-snd`, base `49ad5d67` (`e4896481` plus the brief).
Figures are ASSEMBLED bytes off the image and its symbol map
(`tests/unit/t_sndonce.py` prints every keep). One kernel line changed, at
the end: `DRVM_IMG_SND` 12 -> 11 in `kernel/driver.inc`, the Drivers page's
price of the image, which `t_drvmem` (fast) holds to the image's KB - a
value, so no kernel's size moved; nothing in `drivers/os88drv.inc` or
`apps/os88api.inc` changed and no ABI moved.

The pass ran in two rounds: the brief's goal (commits `5f5df415`,
`23b5adde`), then round 1's WANTS THE OWNER 1 and 2, which the owner
approved (`61c6fa09`, TAKEN AT THE OWNER'S WORD below); its 3 was not.

## BASE AND TIP

| SOUND.DRV (shipped, PicoMEM in) | base `49ad5d67` | round 1 `acd7f4eb` | **tip** |
|---|---:|---:|---:|
| no mixer, no PicoMEM, no MPU-401 (`snd_keep`) | 6,392 (7KB) | 6,106 (6KB) | **5,797 (6KB)** |
| no mixer, a PicoMEM (`snd_keeppm`) | 6,392 (7KB) | 6,106 (6KB) | **5,913 (6KB)** |
| no mixer, an MPU-401 (`snd_keepmpu`) | 6,392 (7KB) | 6,106 (6KB) | **6,098 (6KB)** |
| a mixer (`snd_keepmix`) | 9,937 (10KB) | 9,551 (10KB) | **8,622 (9KB)** |
| image (unpacked; the load claim) | 11,737 (12KB) | 11,369 (12KB) | **10,460 (11KB)** |

| `NOPICOMEM=1` | base | round 1 | **tip** |
|---|---:|---:|---:|
| no mixer, no MPU-401 | 6,209 (7KB) | 5,949 (6KB) | **5,756 (6KB)** |
| no mixer, an MPU-401 | 6,209 (7KB) | 5,949 (6KB) | **5,941 (6KB)** |
| a mixer | 9,754 (10KB) | 9,394 (10KB) | **8,465 (9KB)** |
| image | 11,224 (11KB) | 10,864 (11KB) | **9,945 (10KB)** |

**Both goals are met**: the no-mixer keep is under 6,144 for every machine
class (the worst, an MPU-401 and no mixer, by 46 bytes), and the mixer keep
is 594 under the bonus's 9,216. No machine keeps more than at round 1 or at
the base.

Every other includer of a `drivers/sound/*.inc`:

| includer | includes | base | tip |
|---|---|---:|---:|
| `MIXER.O88` (`apps/mixer`) | `sndpkg.inc`, `apps/os88mixui.inc` | image 3,467 | **3,353** (-114: the rectangle table; its band stays 48 rows, bss 1,056) |
| `VIDSND.O88` (`tests/vidbench/vidsnd.asm`) | `picomem.inc` | 5,632 | 5,632 - same size, not byte-identical: the four attach-only tables moved inside `PM_ONCE`, which this package expands in place |
| `tests/picomem/sbhx.asm`, `vshx.asm` | `picomem.inc`, sb.inc cut by `tests/picomem.py` | - | assemble; the slice still finds `sbl_hook`/`sbl_cell` |
| MIDIRack, RealDMG, XTMP3 (all arms), the Mixer's verbs | `sndpkg.inc` only | - | byte-identical (`sndpkg.inc` untouched) |
| `SOUND.DRV`'s HMA image (`sound.hma.bin`) | all | - | builds (`make`) |

And every includer of `apps/os88gfx.inc`, whose `GFXE_BAND_SEG` is new and
opt-in: **byte-identical** at the same commit, the build directory's md5s
taken before and after (`make`, `make small`, `make emu`, `make mega`) -
CYCLONE, DESKCAL (and its small arm), DESKCLOK (and its small arm),
MISSILE, MIXER, PIXEL (and its kern_mega arm), the Sticky App library
`stiklib.bin` (kern_big, kern_small, kern_emu, kern_mega), STIKNOTE, TANK
(and its small arm), WIRE, WIREFLEET (`wireflet`), SAVER.DRV, and
`tests/stikprob` in both its arms (STIKPROB, DESKPROB), assembled by hand
before and after. Thirteen packages, SOUND.DRV the fourteenth. What did
change in the build tree is SOUND.DRV, and the kernel images and the kernel
modules cut from them (`ctrl`, `desk`, `diskops`, `dock`, `extd`, `format`,
`hiber`, `serial`, `stikmenu`, `win95`, and kern_small's `fdlg`/`filecp`)
plus SYSCHECK - every one of them the `DRVM_IMG_SND` constant.

## TAKEN

Bytes are `snd_keep` / `snd_keepmix` (shipped build). Nothing below changes
what any machine does; hot paths are named with their cycles.

### Where code lives

* **An SB Pro stereo stream's mixer half into the MIXER's run** (`sb.inc`'s
  new `SBL_PRO` macro, expanded by `sound.asm` after `mixer.inc`):
  `sbl_pro_st`, a new `sbl_pro_end` (the 0Eh restore `sbl_stop_stream` had
  inline) and `sbl_m0e`. **-112 / +6.** It is the only code in `sb.inc` that
  touches the CT1345, and it is reached only for a stream verb 11 accepted as
  stereo on a DSP 3.xx - which `snd_sb_pub` allows only with `[mix_kind]` = 2
  - and, for the restore, behind `[sbl_pst]`, which only `sbl_pro_st` sets.
  SPEC.md 34.15.1 already argued exactly that guard for the call; the code
  simply had not followed the argument. `t_sndonce`'s doors are now
  `sbl_hw_start` (the caller) and `sbl_stop_stream`, where `sbl_pro_st` was.
* **The PicoMEM's attach-only tables past `snd_keepmix`**: `pm_irqs`,
  `pm_pmirq`, `pm_sbports` and `pm_irq` are read and written only by `pm_init`
  and `sbl_f_irqdisc`, both inside `DRVV_ATTACH`. Into `PM_ONCE`. **-18 / -18.**

### Folds and shapes

* **`opl_keyon` halves 6208 instead of reading `opl_fmax`**: block b's
  ceiling is exactly `6208 >> (7 - b)` for all eight (an `%rep` asserts it
  against the formula); the scan runs down from block 7 and stops at the
  smallest block that holds the frequency. Checked exhaustively against the
  table for every frequency 19..6208. SI is no longer used, so its push/pop
  went. **-23 / -23.** Not hot: once a note.
* **Verbs 5 and 6 are one body** (`sbl_v_rw`, BP = 1 for a stage): the range
  check on the pool's side, the chunk loop once, the stage the no-exchange
  case. DX was never touched and is no longer saved. BP is free - the kernel
  banks it round every driver call and `sbl_stream_op` already spends it.
  With `sbl_v_close` taking `sbl_v_feed`'s stale exit: **-41 / -41.** Cycles:
  per 512-byte chunk a stage adds `or bp, bp` / `jnz` taken (~19) and a read
  that plus the exchange (~65) and an `mov es, [sbl_poolseg]` it used to do
  once - under 1% of the ~6,800-cycle chunk it is inside, and neither verb is
  on a listed hot path (the per-sample copy is `sbl_fill_guard`'s, untouched).
* **The two opens share `sbl_rec`** - the record's eight common stores,
  `pm_rchk`, the new generation and the publish, last, as both did. **-28 /
  -28.** Once an open.
* **A grant's owner rides in `sbl_gr_act`'s low byte** (100h + instance, so
  the word is never 0 - the kernel's 0FFh included): `sbl_gr_inst`'s 16 bytes
  of state and the claim's extra store went. Every reader already compared
  the owner as a BYTE behind the word's own test. **-25 / -25.**
* **`sbl_cell` masks the line** for both its callers (`sbl_hook`,
  `sbl_unhook`), dead `CLC`s after a `TEST`/`XOR`/equal compare (`sbl_dsp_wr`
  - one fewer instruction on every DSP write, ISR included - `sbl_f_reset`,
  `mpu_put`, `mpu_v_open`, `mpu_v_close`, `sbl_v_feed`, `sbl_v_grant` x2,
  `sbl_v_read`, `sbl_dma_get`), `sbl_v_busy` as `mov al, [sbl_str_act]` /
  `cbw` (the byte is 0 or 1 at every store), `sbl_hcheck`'s equal compare as
  the CF = 0: **-28 / -28** together.
* **`opl_owned`** compares the owner byte in place (no AX) and shares its
  tail; **`opl_claim`** and **`opl_regok`** share theirs (`CLC_OR_STC`):
  **-13 / -13.**

### The mixer's run (`snd_keepmix` only)

* **`os88mixui.inc`'s ten fixed rectangles are a table** of five-byte rows
  drawn in the same order where `MX_RECT` was twenty bytes of stores and a
  call each: **-114** (and -114 of `MIXER.O88`). The five thumb rectangles,
  whose x is a variable, keep the macro. *(`apps/os88mixui.inc` is in no
  agent's row - taken here, being SOUND.DRV's popup.)*
* **SOUND.DRV's band is 44 rows** (`MIXUI_H`, default 48): the last ink is
  the long scale marks' row 43, and `volume_popup` fills the panel white
  before the first band and draws nothing else inside it, so rows 44..47 were
  white written over white. **-88.** `MIXER.O88` keeps 48 - its window's
  ground under those rows is not proven white, so it is not touched.
* **`mix_get`'s two identical `and` arms** are one, and **`mix_to_pct`**
  rounds in byte arithmetic (`mul dl`, `adc ah`) - the quotient is <= 100 for
  every max and value the masks allow, checked exhaustively: **-16.**

**Pixel identity** of the two `os88mixui.inc` changes is checked by a
scratch unicorn harness that runs the base's and the tip's `mixui_draw`
(`SOUND.DRV` images, the kernel's API page as `retf`s and a fixed random
font behind `OSAPI_FONT_GLYPHS`) for every value 0..100: the tip's 44 rows
equal the base's first 44, and the base's rows 44..47 are all white
(181 inked bytes in the band, so the comparison is not of two blanks).

## REFUSED

* **`sbl_arm_half` through `sbl_dma_auto`** (~-25): it is the DSP 1.x's
  re-arm from INSIDE the block ISR - the "audible gap mitigation" - and the
  call, `sbl_dma_auto`'s own push/pop and the register moves are ~60 cycles
  added to that gap. Hot path.
* **A `min(CX, SBL_DCHUNK)` helper for the four chunk loops** (~-12): two of
  them are `sbl_fill_guard`'s, the path that moves every sample, and
  `sbl_live`'s header already refuses a call there.
* **`OS88_RET_LADDER`** (`drivers/os88drv.inc`): most of the driver's pop
  shapes need a rung that is not labelled, and labelling one is an edit to
  the frozen, fingerprinted SDK (`tools/abilock.py`); the one labelled rung
  (`os88_r_si`) has too few users below `snd_keep` to pay its six bytes.
* **`MIXUI_H` 44 for `MIXER.O88`**: see above - not proven pixel-identical.

## TAKEN AT THE OWNER'S WORD (round 2, `61c6fa09`)

WANTS THE OWNER 1 and 2 of round 1, approved; 3 was not, and is not taken.

### 1. Four keep points: `[tiers] [PicoMEM] [MPU-401] [mixer] [attach]`

`picomem.inc`'s `PM_RES` (`pm_runaway`, `pm_tick`) and `mpu.inc`'s
`MPU_RES` (`mpu_put`, `mpu_hush`, verbs 2..5's bodies) are macros expanded in
place without `SND_SPLIT` - so `tests/picomem/` and `vidsnd.asm` assemble
the code they always had, `pm_runaway` asking `[pm_up]` and each verb
`[mpu_base]` itself - and by `sound.asm` into runs of their own between
`snd_keep` and the mixer's. The attach keeps up to the end of the LAST run
it found a user for: `[mix_kind]`, then `[mpu_base]`, then `[pm_base]`.

* **The doors.** `sbl_tick` asks `[pm_up]` before `call pm_runaway` (the
  test that was the callee's first instruction - so a machine without the
  card spends a compare and a taken jump in IRQ0 where it spent a call, a
  compare, a jump and a ret: FEWER cycles). `pm_tickpub`, resident, publishes
  `pm_tick` only with `[pm_base]` - the keep's own test. `snd_pkg` refuses
  verbs 2..5 with `SNDE_NOMIDI` without `[mpu_base]`, the verbs' own answer
  word for word, so their bodies drop the test under `SND_SPLIT`.
  `mpu_detach` stays resident and reaches `mpu_hush` only with `[mpu_own]`,
  which only a successful `SNDV_MIDOPEN` sets.
* **`t_sndonce`** now knows five runs: rule 2 is "no reference into a LATER
  middle run outside that run's doors", run by run (a run kept for a later
  one's sake is free to name an earlier one). Broken on purpose twice:
  `sbl_tick` out of the PicoMEM's doors and `mpu_detach` out of the MPU's,
  and it named `pm_runaway` and `mpu_hush`.
* **No machine keeps more**: the four figures in BASE AND TIP are each at or
  below round 1's single no-mixer figure (6,106), the MPU case carrying the
  PicoMEM's 116 bytes it carried before.

### 2. The popup's band in the popup's claim

* **`apps/os88gfx.inc`'s `GFXE_BAND_SEG`** (opt-in; a word holding the
  band's segment). Every band byte the TEXT routines write goes through ES
  (`GFXE_BS_` = `es:`), `gfxe_bclear` and `gfxe_bfill` load ES from the word,
  and `gfxe_glyph` copies a glyph's eight rows out of the kernel's font into
  `gfxe_gly` (8 bytes) first, so ES is free to be the band while the rows
  are merged (`GFXE_FS_` drops the `es:` on the font reads). It `%error`s
  with LINE, BITS, SCALE, the band utilities or the Sticky App library - the
  paths it does not cover. `os88mixui.inc` passes it on as `MIXUI_BAND_SEG`
  and blits from that segment. Every other includer is byte-identical (the
  table above).
* **`volume.inc`**: the band is at offset 0 of the save-under claim
  (`MIXUI_BUFFER` 0, `MIXUI_BAND_SEG volume_save`), the pixels after it
  (`OSAPI_GFX_SAVE`/`_REST` at `VOLUME_BANDSZ` = 968). `mixui_draw` runs
  only while that claim is held. `volume_band` (968 bytes of every mixer
  machine's keep) is gone: **-929** of `snd_keepmix` net.
* **The Dark theme's icon** borrowed 48 bytes of the band to invert the
  speaker. It inverts `mix_speaker` IN PLACE, blits, and inverts it back:
  nothing else reads the bitmap and the UI task is in the routine for both
  passes. A unicorn harness ran the base's and the tip's `mix_icon` for both
  displays and both papers and compared the blit's registers and the 48
  bytes it was handed: identical, and the bitmap restored.
* **Pixel identity**: the same harness as round 1, with the tip's band in a
  segment of its own - identical to the base's first 44 rows for every value
  0..100, and the only image bytes `mixui_draw` writes are its own and the
  library's state words (`gfxe_gly` among them).
* **Cycles** (counted in the harness, priced, not timed on a machine):
  +236..264 instructions and +277..310 segment prefixes a `mixui_draw` -
  the glyph copies (~11 instructions and a 4-word `rep movsw` a glyph) and
  an `es:` on every band write. Roughly **+4,000 cycles, ~0.85 ms on a
  4.77MHz 8088, a slider redraw**, against a redraw of ~5,000-5,700
  instructions before its blit. Not a listed hot path: it is the popup's
  drag.
* **The claim, per adapter** (72 rows x 26 bytes x planes, +968): mono
  1,872 -> 2,840, **2KB -> 3KB**; VGA/EGA 7,488 -> 8,456, **8KB -> 9KB**;
  VESA 8bpp 14,976 -> 15,944, **15KB -> 16KB**; VESA 16bpp 29,952 ->
  30,920, **30KB -> 31KB**. It crosses a KB on every adapter - for as long as
  the popup is up, from the heap, where the 968 bytes were resident for the
  session on every mixer machine before. `tests/mixer.py` asserts the new
  figure (it asserted the old one and went red, as it should).
* **`DRVM_IMG_SND` 12 -> 11** (`kernel/driver.inc`, in fsys's file - the one
  line, forced by `t_drvmem`, which failed `make` until it moved): the image
  is 10,460 bytes, 11KB.

## WANTS THE OWNER

None open. Round 1's three were: (1) the device keep points and (2) the band
in the popup's claim, both TAKEN above; (3) ordering the runs `[core]
[mixer] [MPU] [PicoMEM]`, which would have made a no-mixer machine with an
MPU-401 or a PicoMEM keep the mixer, NOT approved and not taken.

## WANTS AN ABI BREAK

None.

## CROSS-FILE

* `kernel/driver.inc` (fsys's row): `DRVM_IMG_SND` 12 -> 11, one value,
  forced by `t_drvmem` (above). No other line of the kernel changed.
* `apps/os88mixui.inc` and `apps/os88gfx.inc` (no agent's row) are taken
  above, and said so; `tests/mixer.py`'s claim figure follows the band.

## ROWS RUN

**Round 1** (after `23b5adde`, `--marty-jobs 1`): 23 ok, 0 failed -
`sndonce`, `sndkeep`, `sndmove`, `sndplay`, `recorder`, `sndfmt`, `sndcap`,
`ringmove-audio`, `sbirq5`, `picomem`, `pmlib`, `pmmarty`, `mpupin`,
`midirackmpu`, `midiracksb`, `midirackfm`, `fmpatch`, `fmrefuse`,
`gorillasmusic` (which breakpoints `opl_keyon` and reads the pitch),
`covoxdrv`, `mixerhost`, `mixervga`, `mixersb16`. (`pmlib`/`pmmarty` first
SKIPPED for want of `libpmhost` and `mpupin` FAILED on a missing `mcopy`;
`tools/picomem/build.sh` and `apt-get install mtools` fixed both.)

**Round 2** (after `61c6fa09`): **26 ok, 1 failed** - the round-1 set plus
`mixercga`, `mixerherc`, `mixervesa` (the popup's claim on every adapter)
and `dosest` (which reads `DRVM_SND`). The five `mixer*` rows first FAILED
on `save-under has the adapter-sized claim` - the row asserted the claim
without the band, the change it exists to see - and pass with
`tests/mixer.py` taught the band (+1KB on every adapter, above).

**`recorder` FAILS and it is not this change**: `FAIL 5: Stop left state 1
('RECORDING...')` - the Stop picked from the Sound menu does not stop the
take - in every one of nine runs this round (one of them the same leg's
`took 6144 bytes` in 0.5 s instead). It fails IDENTICALLY with round 1's driver, library and kernel
sources put back over the tip and rebuilt (it passed at round 1), and with
the attach forced to keep the whole image - so neither the layout nor any
byte here is the cause. The box was at load 5 on 4 cores (five agents).
Its record path - verb 4, verb 6, the ISR's input leg - is unchanged by
round 2. **Re-run it alone on a quiet box before believing it**
(`os88bisect.py classify recorder`).

Also: `t_sndonce` broken on purpose in both rounds; `make`, `make small`,
`make emu`, `make mega` build at the tip; the fast tier is 66/66. No row
reaches an SB Pro's stereo stream (MartyPC's card is a DSP 2.01, QEMU's an
SB16), so `SBL_PRO` is verbatim code moved, its guard argued above and held
by `t_sndonce`. Not run: `waverackrec` (2,400 s declared).

## DEFECTS FOUND

* `sbl_v_open_in`'s header says "`sbl_total` holds the capture CAPACITY";
  nothing in verb 4 writes `[sbl_total]` since SPEC.md 34.6.5 retired the
  grant capture, and nothing reads it for an input stream (verb 1 refuses
  one, the ISR's input leg and verb 9 read `[sbl_consumed]`). A stale
  comment, not a defect in behaviour; left for the next edit of that header.
