# Kernel size pass 10 - agent "covox": notes

Files: kernel/ctrl.inc, kernel/snd.inc, drivers/sound/{sound.asm,covox.inc,
sndpkg.inc}. Special job: the Covox Speech Thing (#229, fe9a559), which had
never had a size pass - +292 CTRL.DRV (big) / +258 (small) and +259 of
SOUND.DRV's assembled image at that commit.

## TAKEN

### Batch 1 - the Covox, both halves (no resident byte moves)

kern_big and kern_small `kernsize` sections: +0 / +0 (all of it is CTRL.DRV
and SOUND.DRV).

| artefact | base | batch 1 | delta |
|---|---:|---:|---:|
| CTRL.DRV kern_big (plain) | 11,450 | 11,301 | -149 |
| CTRL.DRV kern_small (image) | 4,621 | 4,490 | -131 |
| SOUND.DRV sound.bin | 6,721 | 6,637 | -84 |
| SOUND.DRV image (drv) | 6,529 | 6,445 | -84 |

CTRL.DRV (kernel/ctrl.inc):
* `cp_snd_lptok` deleted: a Covox port is `cp_snd_rowok`'s entry
  CPS_NROW + n (= its tier SND_RT_LPT + n), the table grown by 3 bytes and the
  row shortcut skipped for a port (+4).
* `cp_snd_radios`: ONE loop over the seven glyphs (rows step down, ports step
  across); the port's dot is "the Covox row owns the dot and this glyph is
  [snd_route]".
* `cp_snd_paint`: labels through a `CPSTAGEX` table instead of a four-arm
  ladder; CX/DX stepped rather than recomputed by `mul`; the port loop runs BX
  on from the rows, the digit staged from 'LPT' + ('1' - CPS_NROW).
* `cp_snd_click`: the tier row by one divide instead of four compares (guarded
  by %if on the band geometry); the port path's `cmp`+`sub` folded; `.got`
  shared by the label's first-answered search; "already a Covox that sounds"
  is `cp_snd_row == 3`; the Test button's out-of-range `jg .done` (a relaxed
  5-byte jcc) goes to a near `.tout`.
* `cp_snd_row`: [snd_route] loaded once; LPTDAC tested as a byte.
* `cp_snd_tiers`: its fourth entry was never read (row 3 branches to .lpt
  first) - gone.
* NOT Covox, same file: `cp_vid_click` had a relaxed 5-byte `jl .rows`; the
  radio rows now sit between the button and the desktop row, so every jump is
  short and the `.donej` trampoline is gone.

SOUND.DRV (drivers/sound/):
* `cvx_latch` written in line in `cvx_probe` (its one caller) as a two-value
  loop (AAh xor FFh = 55h, SF ends it); the verdict is CF and the restore path
  keeps it, so no pushf/popf; cvx_ports stored unconditionally (the base or 0
  via `sbb/and`); the range test `cmp dh, 4`; loop ends on the tier bit's SF;
  the "none answered" CF is one `cmp byte [si+DSV_TIERS], 10h`; ES not saved
  (drv_stamped restores it). Ends by falling into `snd_upnm`.
* `snd_upnm`: the MPU-401's and the Covox's "first one up names the row" tail,
  shared.
* `cvx_tier` became `snd_tier`'s tail; the tier rides the stack instead of
  `[cvx_ask]` (byte gone); index through DI (NOT BX: drv_tier_x reads the row
  through BX after the call - the first version clobbered it and covoxdrv
  caught it); a non-Covox tier reads cvx_ports[-1], a never-written zero.
* `snd_tier`: `cmp ah, SND_RT_SB / je .want` replaces the two-compare
  `>= LPT` / `>= SB` ladder (identical for every AH).
* `cvx_v_info`: `cmp ax, 1` gives CF for zero.
* `inc byte [drv_up]` where it is known 0.

### Batch 2 - resident: cp_open_x, cp_open_need, snd_release_inst

kern_big `.text -4 .cold -8 (sum -12)`; kern_small `.text -4 .cold -6
(sum -10)`.

* `snd_release_inst` (.text, both): `or bp, bp / jz` before `drv_svc_call`
  is that routine's own first test (BP = 0 refuses, CF = 1 AX = 0, no side
  effect) and this routine restores AX and the flags: -4. Teardown, not hot.
* `cp_open_x` (.cold): the beep falls into the refusal's `stc`, one
  `pop ax / ret` epilogue for all three exits: -5.
* `cp_open_need` (.cold): kern_big's `hbf_nodisk` banks AX itself (it ends
  in `kretc_cx`), so the push/pop round it went; one shared `stc / ret`: -3
  big, -1 small.

### Batch 3 - the tone expiry's generation compare was a tautology

kern_big `.text -24 .bss -2 .ovlw -10`; kern_small `.text -24 .bss -2
.ovl -10`.

`snd_tick` compared `[snd_texp_gen]` with `[snd_town_gen]` before silencing an
expired tone. The two bytes were written in exactly one place
(`snd_tone_req`'s grant, from AL, in one IF=0 window) and zeroed together in
`snd_init` - so they were equal on every tick that ever read them, and the
compare could never refuse. Both bytes, both stores, both inits and the
compare are gone. `snd_tick`: on the expiry tick `dec / jnz / mov / cmp /
jne / jmp` became `dec / jz` (the tail call); on every other tick the path is
unchanged (`cmp / je` or `cmp / je / dec / jnz`, one instruction fewer
there: `jz` not taken then `ret`, against `jnz` taken to `ret` - the same
count). `snd_tone_req`'s two paths now share `inc [snd_gen] / mov al` at
`.granted` (inside the same window). SPEC.md 34.3 updated: the owner's
atomic grant is what makes the expiry its own.

### Batch 4 - snd_town_off

kern_big `.text -3`; kern_small `.text -3`. The zero is made once, before
the test (`cmp [snd_town_act], al` is a byte shorter than against an
immediate), and remade only on the path that called a sink, which may be a
driver's and need not keep AX; `[snd_ch2mode]` is stored from AL. Reached
from snd_tick's expiry (IRQ0): 9 instructions either way, 3 bytes fewer.

Checked on MartyPC (base and tip alike): a tone record poked live with
exp = 5 is silenced by snd_tick - act, exp and ch2mode all 0 after.
`soak -k sndplay` ok.

### BUG FIXED (its own paragraph in the commit)

`snd_entry`'s DRVV_READY jumped to `.nosb` - written when that label was the
end of attach. The MPU-401 (34.13) and Covox (34.14) probes were later added
BELOW it, so every load re-ran both at READY: the MPU's reset and UART
command sent again with [mpu_base]/[mpu_own] cleared and re-found, and the
three LPT latches written again. READY now jumps to `.ready`, past the
probes, which is what its comment always said. 0 bytes.

### Totals at the tip

* kern_big: `text 44,277 -31  bss 5,131 -2  cold 37,913 -8  ovlw 5,001 -10
  (sum -51)` - resident -41. snd.inc 831 -> 800 code, 37 -> 35 bss;
  ctrl.inc 360 -> 352 code.
* kern_small: `text 32,873 -31  bss 3,102 -2  cold 23,944 -6  ovl 2,076 -10
  (sum -49)` - resident -39.
* CTRL.DRV: kern_big 11,450 -> 11,301 (-149); kern_small image 4,621 ->
  4,490 (-131).
* SOUND.DRV: sound.bin 6,721 -> 6,637 (-84); image 6,529 -> 6,445; no bss
  (a driver's state rides in its image).

What the Covox costs now, by listing: SOUND.DRV ~164 bytes against the 259
#229 added (-95, 37% - covox.inc itself 210 -> 132 including the shared
snd_upnm and the cvx_ports[-1] word); CTRL.DRV ~150 against 292 (~48%; the
rest of the -149 is the general Sound/Display page items above). Half of
SOUND.DRV's was not reached without dropping something - see REFUSED.

Tests: fast tier 61/61 after every batch; `make small`, `make emu` assemble;
tests/covox.py --arm drv/auto/nolpt/cp all PASS on MartyPC at the tip (cp's
five screenshots byte-identical to the base's); `soak -k sndplay` ok;
stkbalance over the four files: 0 unbalanced, base and tip.

## REFUSED

* SOUND.DRV: dropping the Covox's DSV_NAME ('Covox' + the store, ~11 bytes):
  nothing in the kernel reads DSV_NAME today, but it is a published cell.
* SOUND.DRV: cvx_tier re-reading the BIOS table at 0040:0008 instead of
  keeping cvx_ports (~2 bytes net): a BDA that changed after attach would
  change which port is the DAC.
* SOUND.DRV: the `jmp short $+2` I/O delays in the latch test (8 bytes): a
  timing change on fast ISA machines.
* SOUND.DRV: restoring the LPT control register before the data latch would
  end the probe on the base for free (-2); it reorders port writes that
  lp_latch makes data-then-control.
* SOUND.DRV `sbl_isr` (sb.inc, not Covox): `je .spur` and `jne .input` are
  relaxed 5-byte jccs, and the hot path pays for them (+12 cycles each per
  block IRQ: the skip is taken). Moving `.spur` and `.input` up behind the
  output path makes both short but pushes `jne .direct` one byte out of
  range: -1 byte net, a reshuffle of an ISR for it. Worth doing with the
  speed as the reason, by whoever next touches sb.inc.
* snd.inc `snd_evtmp` (8 bss, kern_big) overlaid on `snd_patch`: a worker
  pre-empted between osapi_snd_fm_x's staging and the driver's read would
  have its patch overwritten by a clip's abort drain.
* snd.inc `snd_str_busy`'s `jnc / xor ax, ax` (4): drv_svc_call's refusal
  already answers AX = 0, but a DRIVER answering CF = 1 with AX != 0 would
  then read as busy.
* ctrl.inc `cpf_cp_onup`/`cpf_cp_ondrag` sharing a mod_live helper: 0 on
  kern_big, +4 on kern_small.

## CROSS-FILE

* docs/INDEX.md is regenerated in this branch for this notes file (one line);
  every agent's branch will conflict on that line - re-run
  `tools/os88index.py` after the merge rather than resolving by hand.
