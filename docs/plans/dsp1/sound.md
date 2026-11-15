# Driver size pass 1: sound (`drivers/sound/`, SOUND.DRV)

Agent **sound**, branch `dsp1-sound`, cut from `de93581a`. Six commits, all
of them in `drivers/sound/` (plus this note and `docs/INDEX.md`). The pass
was cut short by the coordinator's wrap-up; section 10 is what was queued.

## 1. BASE AND TIP

Assembled at the base and at the tip, the same `nasm` lines as the Makefile.
"file" is the lz4-packed `.drv` (`os88drv.py`). SOUND.DRV has no trailing
bss: its state is above the attach-only tail and ships as zeros.

| build | image base | image tip | delta | file base | file tip | delta |
|---|---:|---:|---:|---:|---:|---:|
| SOUND.DRV (shipped, PicoMEM in) | 10,478 | **10,265** | **-213** | 8,988 | **8,889** | **-99** |
| `NOPICOMEM=1` | 9,963 | 9,750 | -213 | 8,520 | 8,426 | -94 |
| SNDDIAG (`build/snddiag/sound.drv`) | 11,328 | 11,112 | -216 | 9,596 | 9,499 | -97 |

The file falls far less than the image because 32 of the image bytes were
zero padding (mixer tables) that lz4 already carried for almost nothing, and
because the driver packs to 86.6%.

What RAM keeps after the attach's shrink (`DRV_H_KEEP`, SPEC.md 34.15.1) -
the kernel rounds it up to 512 (driver.inc's `%1_att`), but these are the
bytes:

| machine has | base | tip | delta |
|---|---:|---:|---:|
| nothing but the tiers (`snd_keep`) | 5,828 | 5,784 | -44 |
| + a PicoMEM (`snd_keeppm`) | 5,944 | 5,900 | -44 |
| + an MPU-401 (`snd_keepmpu`) | 6,129 | 6,072 | -57 |
| + a mixer (`snd_keepmix`) | 8,644 | 8,505 | -139 |

Clusters: SOUND.DRV is 18 clusters on `os8088-360.img` at base and at tip
(8,988 -> 8,889 bytes); `os88diskuse --diff` reports 37 -> 37 free on the
360KB system disk. No other 360KB image carries it. The 720KB, 1.2MB,
1.44MB and kern_mega system disks carry the same file, 99 bytes smaller.

## 2. TAKEN

Every item is equivalence-checked by a scratch differential harness (below)
run on the base and the tip images. Hot means section 1's hot paths.

| item | where | image | hot? | how shown |
|---|---|---:|---|---|
| `mpu_alone`, `mpu_waitw`, `mpu_ack`: one CF tail each (CLC_OR_STC, the TEST's CF = 0, an EQUAL compare's) | mpu.inc, attach-only | -9 | cold | attach harness |
| `mpu_v_open` without BX | mpu.inc, MPU run | -2 | cold | attach harness |
| `mpu_v_write`'s tails share the pops (`xchg ax, di`; CF = 0 already on the success path) | MPU run | -5 | per-call tail: success 8 cycles FASTER (no CLC, XCHG for MOV), the timeout path +11 (a JMP) - an error path after 4,096 polls | attach harness |
| `mpu_v_close` calls `mpu_detach` (its body) | MPU run | -6 | cold | attach harness |
| mixer tables lose their zero padding: a count byte in front of each, `mix_desc` and the save/detach walks use it | mixer.inc | -32 data, -14 code | cold | mixer + volume harnesses |
| `mix_rd`/`mix_wr` share `mix_idx`; `mix_to_pct` does its callers' shift and mask; `mix_get`'s stereo flag by `cmp dl,[si+1] / adc dl,dl`, DI from `mix_desc`'s zero-extended AX; `mix_info`'s dead CLC | mixer run | -31 | cold (UI; +1 call per mixer access) | mixer harness |
| `mix_probe`: INFO's mask from the table's count, no CX bank round the test | attach-only | -7 | cold | mixer + attach harnesses |
| volume popup: one far bridge call (`volume_br1/0/br`, a `cmp ax, imm16` skip), key arms share their clamps (`sub/adc` for Left), `volume_level` inline, `volume_draw` tail-jumps, a no-op JMP and a redundant `or al, al` gone, the held-press drag joins `.grab` | volume.inc | -23 | cold (UI) | volume harness (66 scripted sessions: keys, presses, drags, no memory, no save, off the icon) |
| `sbl_tc_out` (40h + TC) falls into `sbl_dsp_wr`, both starts call it | sb.inc | -8 | open path; `sbl_dsp_wr` itself unchanged | stream harness |
| `sbl_pool_set`: the pool's base/size store, once; `sbl_pool_grow` shifts DX by CL | core | -12 | cold | stream harness |
| `sbl_v_grant`'s exits reordered | core | -4 | cold | stream harness |
| `sbl_v_fmt` tests the unpublished caps against AH directly | core | -5 | cold | stream harness |
| dead CLCs: `sbl_grant_find` (EQUAL compare), `sbl_grant_chk` (SUB cannot borrow - proved in its comment), `sbl_v_busy` and `snd_ident` (their dispatch's SHL of a byte), `snd_rmode` (both ways in), `mix_info`, `mix_desc` | core/mixer | -6 | `grant_chk` is on every stage/read: 4 cycles faster | stream / attach harnesses |
| `sbl_stop_stream` masks the 16-bit channel with `[sbl_hdma]` itself (5/6/7 & 3 \| 4 is 5/6/7) | core | -4 | cold | stream harness |
| `sbl_dleft`'s 16-bit count port from the channel number | core | -2 | **hot** (the kern_mega ISR's direct path, and verb 9): **52 cycles against 60**, 8088 terms below | stream harness (ISR delivered, CPU_F_PM both ways) |
| `sbl_hw_start` tests wide before high-speed (never both) | core | -2 | open path | stream harness |
| `sbl_v_open`'s refusal codes chained by a `cmp ax, imm16` skip | core | -4 | refusal paths | stream harness |
| `opl_fm_op`'s rollback flag `sbb al, al` | core | -2 | note-on: 8 cycles faster | attach harness |
| `opl_probe`'s tail CLC_OR_STC, no dead `[opl_kind] = 0` store (the image's zero state) | attach-only | -7 | cold | attach harness |
| `opl_init`: AL = 0 once per loop; no AX/CX bank (`opl_probe` banks); tail-jumps `opl_defpat` | attach-only | -7 | cold | attach harness |
| `opl_kind_probe` banks nothing `opl_probe` already banks | attach-only | -8 | cold | attach harness |
| `sbl_hdma_find`: AL = AH (the mask byte IS the channel), the port from the channel, the unread CF work gone | attach-only | -11 | cold | attach harness |
| `sbl_f_probe`: one jump in the scan, the version stored as a word, `.no` stores AX = 0 | attach-only | -7 | cold | attach harness |
| `sbl_dsp_rd`: no CX/DX bank (`sbl_f_probe` banks), no CLC | attach-only | -5 | cold | attach harness |
| `sbl_irq5_taken`: the compare's CF is the answer | attach-only (and `sbirq5`'s slice) | -5 | cold | attach harness; `tests/picomem/sbhx.asm` assembled with the new slice |
| `snd_attach`'s FM leg `inc byte [drv_up]` | attach-only | -1 | cold | attach harness |

`sbl_dleft`, 8088 `max(clocks, 4 x bytes)`: base `mov dl,[m]` 16, `and dx,3`
12, `shl dx,1` 8 + 8, `add dx,0C2h` 16 = **60**; tip `mov dl,[m]` 16,
`shl dl,1` 8 + 8, `add dl,0B2h` 12, `mov dh,0` 8 = **52**.

**Hot-routine parity.** `tools/os88rdiff.py` with 32 hot routines (the ISR,
`sbl_dsp_wr`, `sbl_arm_half`, `sbl_go_on`, `sbl_halt`, `sbl_resume`, the feed,
status, ppos, copy, fill and refill paths, `sbl_tick`, `pm_runaway`,
`pm_tick`, `mpu_put`, `mpu_v_write`, every OPL write path, `snd_pkg`) reports
every one `same` at an EVEN shift, in the shipped arm and in `NOPICOMEM`,
except the three changed on purpose above (`sbl_dleft`, `mpu_v_write`,
`opl_fm_op`). Two items were shaped for it: `sbl_v_fmt`'s five bytes brought
the stream verbs back to even, and `mpu_v_info` KEEPS its redundant CLC (a
comment says so) because removing it put `mpu_v_write` at an odd shift.

**The harness** (scratch, not committed): `diffh.py` runs a routine in the
base and the tip images under unicorn with SS != DS, the kernel's far cells
at 0060:xxxx and the rare door answered in Python, IRQs injectable; it
compares every register, CF, the port trace, every kernel call and its
arguments, and every state cell (code pointers by name). Four suites, all
**0 differences** at the tip:
- `t_attach` (400 machines): no/OPL2/OPL3 x no/220/240/260 SB x no/330/300
  MPU x LPT sets x 8088/286; DSP 1.05/2.01/3.02/4.05/4.13; the CT1745's IRQ
  register and the F2h discovery (IRQ 5/3/2 delivered), a hooked int 0Dh on
  an XT, the 16-bit channel present/absent/unprogrammable; then READY, TIER,
  HWINFO, FM verbs, tones, releases, package verbs and DETACH.
- `t_stream` (400 sequences of 40, 107 successful opens): grants, stages,
  linear/ring/external/ADPCM4/stereo/16-bit opens at every rate edge, feeds,
  status, ppos, pause, close, busy, format, record opens and reads, ticks,
  refills, block IRQs, releases, heap refusals, CPU_F_PM both ways.
- `t_mixer` (CT1335/1345/1745, none, stuck bus; 300 random verbs each).
- `t_volume` (66 scripted popups).

## 3. REFUSED

| item | bytes | why |
|---|---:|---|
| `mpu_hush`'s pairs through a local "AL, then 0" | -3 image, **+4 file** | the file grew; and with it `mpu_v_info`'s CLC had to stay for parity anyway |
| `sbl_arm_half` through `sbl_dma_auto` + `sbl_dsp_wr3` | ~-15 | it is the single-cycle ISR's re-arm: adds calls and moves, not cycle-neutral |
| `sbl_v_status` / `sbl_v_ppos` sharing their prefix | ~-10 | polled by players; the share adds a compare and a jump |
| `sbl_grant_chk`'s two tails shared | -2 more | adds a JMP on every stage/read; the CLC alone was taken (faster) |
| `mix_probe`'s save loop and `mix_detach` as one walker | -27 image | moves 18 bytes INTO the mixer machine's resident run to take 45 out of the attach-only tail |
| `snd_attach` keeping BX/CX/DX unbanked (drop the probes' callee saves) | ~-20 | the kernel's `%1_att` says "clobbers everything but BX", but DRVV_ATTACH's own contract does not say what a driver may clobber: not ours to read into |
| `pm_*` saves and `pm_undo` | few | picomem.inc is also `%include`d by three test harnesses (`sbhx`, `vshx`, `vidsnd`); left alone |
| `opl_keyoff`/`opl_tone` jump shapes | 2 | IRQ0's key-off path |

## 4. WANTS THE OWNER

None. No wording, limit, probe or behaviour was changed.

## 5. WANTS AN ABI BREAK

None found worth one. `sndpkg.inc` is untouched (values and layout).

## 6. CROSS-FILE

- **Attach-only code handed back, priced** (section 0.2): SOUND.DRV already
  does it - DRV_H_KEEP, SPEC.md 34.15.1 - so the price is what it keeps:
  the attach-only tail is now **1,760 bytes** (10,265 - 8,505), returned on
  every machine; the mixer run 2,433, the MPU run 172, the PicoMEM run 116.
  The kernel rounds the kept figure up to 512 (`driver.inc`'s `%1_att`): the
  tip keeps 12/12/12/17 rungs of 512, as the base did - no rung crossed,
  and the bytes are the figure.
- **`apps/os88mixui.inc`** (shared with Mixer): not read for bytes.
- **SPEC.md 34.15.1's keep figures** (5,797 / 8,622, and the 1.3KB-of-zeros
  sentence in sound.asm's `snd_keepmix` banner) were already stale at the
  base - the state block is ~300 bytes now. They are dated history in the
  SPEC and were left; the banner is a doc defect (section 9).
- **`OSAPI_MEM_P` REGROW** reads no CX (checked in `kernel/memory.inc`);
  `sbl_pool_grow` now arrives with CX = 4xx. Recorded so nobody "fixes" it.

## 7. ROWS TO RUN

All soak unless named: `sndonce` (layout of the runs: new references are
`mpu_v_close -> mpu_detach`, MPU run to core, allowed), `sndkeep`,
`debugdisk` (shipped arm vs a hook-stripped build: no hook was added or
moved out of its line), `snddiag`, `sbirq5` (its slice now holds the new
`sbl_irq5_taken`), `picomem`, `pmbox`, `pmbox2`, `mixerhost`, `mixervga`,
`mixercga`, `mixervesa`, `fmrefuse`, `midirackmpu`, `midiracksb`,
`midiracktone`, `sndfmtmarty`, `covoxauto`, `covoxnolpt`, `recorder`,
`sndmove`, `hmadrv`, `hma286`, `megasndword`, `megasbhold`, `megasbgap`,
`vidrmbsnd`, `buildmatrix`; full tier `megapkg`; fast tier `stkbalance`
and `stkapps` passed in every `make`.

## 8. LOOKED

No boot taken: the attach, probe and ISR-adjacent paths were proved by the
differential harness instead, and the wrap-up arrived first.

## 9. DEFECTS FOUND

- **Not a defect, but a stale banner**: sound.asm's `snd_keepmix` comment
  says "~1.3KB of zeros ship"; the base image's zero runs total ~310 bytes.
  Left (a comment, and out of this pass's subject).
- No functional defect found.

## 10. NOT REACHED

Queued when the wrap-up came, unpriced unless a figure is given:
- `sbl_hdma_auto`: mask from `[sbl_hdma]`, port by `add dl` (~-2; it sits
  before `sbl_isr`, so it needs an even partner).
- `sbl_v_open`'s `test byte [sbl_oflags]` (nine of them, 5 bytes each) held
  in BP after `.irqchk` (~-3, depends on every kernel call keeping BP).
- `mix_icon`'s invert-draw-invert path (~-3).
- `snddiag.inc` itself (debug disk only; its record is the SNDDIAG
  package's ABI).
- A final per-item lz4 audit: items of 1-3 image bytes were judged in their
  batch, and lz4 moves the file +-4 bytes per edit either way.
