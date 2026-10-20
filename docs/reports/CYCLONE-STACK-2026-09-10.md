# Cyclone's stack overflow, reproduced — and the Sound Blaster is the term

**Taken 2026-09-10 at `f11398f`.** MartyPC, `os8088_5150_herc` and
`os8088_5150_herc_sb` — an IBM PC 5150 on the genuine `27 OCT 82` ROM,
Hercules 720, differing in **one thing**: whether a Sound Blaster 2.0 is in
the machine. docs/FIELD-NOTES.md 42 is what this is about.

## 1. Two things had to be fixed before it would reproduce at all

**The repro was never in the game.** Cyclone's title screen says
`PRESS ENTER TO START`; the script pressed Space, and sat on the title for
the whole of every run. **Every Cyclone stack figure this branch quoted before
today is the title screen's depth** — including the "114 of 192, flat" in
docs/FIELD-NOTES.md 42.2.1, which is why that document could not find its
missing bytes. The reporter's procedure names the step (*"Enter game"*) and
the script skipped it.

**And no machine here could host the question.** Seven MartyPC machines carry
a Sound Blaster and **every one is CGA or VGA**, so a 1bpp adapter with a card
in it did not exist. `os8088_5150_herc_sb` is that machine.

## 2. The A/B

The reporter's own procedure: boot, open B:, open `CYCLONE.O88`, **press
Enter**, hold Right + Space. Three paired trials, 30 s of held keys each.

| | slot 4 (`cy_worker`, 192 bytes) | outcome |
|---|---|---|
| Hercules, **no card** | **164 / 192** — three runs, identical to the byte | **survived 3/3** |
| Hercules, **SB 2.0** | 184, 188 sampled before the panel | **PANIC 3/3**, at 12 s, 10 s, 23 s |

The panel: `STACK OVERFLOW  TASK 04  SP 172C`.

**The card is the term.** The walk is deterministic and already at **85%**
with nothing else on the machine; the driver supplies the last 28 bytes.

## 3. The two SPs are the same event at two moments

Slot 4 runs **5,974 … 6,166** on this build (`sch_stacks` + the SPEC.md 8.7
class table: 128,128,128,192,…).

| | SP | where |
|---|---|---|
| the field panel | `0x1788` = 6,024 | **50 bytes ABOVE the base** — the excursion had unwound by the time `sch_switch` looked |
| this repro | `0x172C` = 5,932 | **42 bytes BELOW the base** — caught mid-excursion, in slot 3's bytes |

Both are the same overflow; `sch_diepanel` prints the **parked** SP, so
whether it reads healthy is a matter of when the check landed. That is why
docs/FIELD-NOTES.md 42.2.0 could decode a *healthy* SP off a machine that had
just died.

## 4. The mechanism, from the source rather than from the shape of the result

An SB 2.0 carries an OPL2, so `drivers/sound/sound.asm`'s attach publishes
**both** halves (lines ~113 and ~136):

```
    mov word [snd_services+DSV_TONE], opl_tone     ; the OPL leg
    mov word [snd_services+DSV_TICK], sbl_tick     ; the SB leg
```

and both are entered **from inside IRQ 0, at IF = 0, on whichever task slice
the tick interrupted**:

- **`DSV_TICK`** — `drivers/os88drv.inc`: *"near proc called from `snd_tick` —
  INSIDE IRQ0, at IF=0."* It runs **every tick, whether or not anything is
  playing**, so it is a constant addition to every slice. This is the term
  the idle floor sees: stkdiag reads the floor **32 without a card and 52
  with one** (docs/reports/STKDIAG-PC5150-2026-09-10.md), and slot 1 during
  Cyclone reads **70 against 78**.
- **`DSV_TONE`** — `kernel/snd.inc`'s `snd_tone_out` replaces a *near tail
  jump* to `spk_tone` with `push bp / drv_svc_call` — **a far call into the
  driver** — and its own comment says the path is *"Reached from `snd_tick`'s
  expiry path, so this can run INSIDE IRQ0 at IF=0."* Cyclone fires a tone
  every few frames (`cy_sfx` → `OSAPI_SND_TONE`), so expiries are frequent and
  **asynchronous to the walk**.

That second one is why the symptom keeps changing. Where in the walk chain an
expiry lands is a race, so identical starts panic at 12 s, 10 s and 23 s — and
a clean panic, a corrupted screen and a hard reboot are **three landing sites,
not three bugs**. An 8086 has no fault to triple, so the reboot is simply
execution reaching `F000:FFF0`.

**A correction to this branch's own earlier account:** it was written up first
as an *IRQ 7 DMA completion*. It is not — Cyclone plays no stream, and the
card's IRQ is not in this path at all. It is IRQ **0**, the driver reached
through two service pointers that are null on a machine with no card.

## 5. Which service call — separated with a poke, no build

`snd_rt_card` tests `cmp byte [snd_route], SND_RT_SPK / je .no`, so writing
**1** to `snd_route` sends *tones* back to `jmp spk_tone` while `snd_tick`
keeps calling `DSV_TICK` every tick — a different call site, untouched. One
byte, at run time.

| HEAD, Hercules 720 | slot 4 of 192 | free | outcome |
|---|---|---|---|
| **no card at all** | **164** ×3 | 28 | survived 3/3 |
| SB 2.0, `snd_route = SPK` (`DSV_TICK` only) | **180** ×2, identical | 12 | survived 2/2 |
| SB 2.0, tones to the driver (both) | 184, 188 | — | **PANIC 3/3** |

So the decomposition is exact:

- **`DSV_TICK` costs 16 bytes**, on every slice, all the time — it is called
  from `snd_tick` inside IRQ 0 whether or not anything is playing. 28 bytes of
  margin becomes 12.
- **`DSV_TONE` costs more than the 12 that are left**, and it arrives
  asynchronously, which is why the panic time varies.

**Neither is enough on its own and together they are.** That is the whole of
docs/FIELD-NOTES.md 42's missing term.

Two cautions before anyone calls `snd_route = SPK` a fix: 12 bytes is thinner
than any declared class margin in the tree, and it takes the FM tier away from
everything else on the machine. It is a **diagnosis**, and it happens to be
reachable from Control Panel → Sound without a build.

## 6. The history sweep — the inlining is worth 18 bytes of it

`cy_worker`'s peak on the **no-card** machine, which reads a deterministic
number where the SB machine reads a coin toss. Each point built in its own
worktree, every one given the same period ROM so the BIOS is held fixed.

| point | commit | slot 4 of 192 | free |
|---|---|---|---|
| wave 5 — the walk moves into the apps | `94dd890` | **182** | 10 |
| the commit before the `gfx_points` inlining | `0d43c61` | **182** | 10 |
| the inlining, with its 34-byte caller cost | `189c8c7` | **164** | 28 |
| HEAD, caller cost back to 26 | `e6f6fc0` | **164** ×3 | 28 |

**Between `0d43c61` and HEAD the only code change in the entire tree is
`kernel/vga12.inc`** — `git diff --stat 0d43c61 HEAD -- apps/ kernel/
drivers/` is that one file — so the attribution is clean: **the `gfx_points`
inlining took 18 bytes off Cyclone's deepest chain.** It removed the nested
`gfx_ls_addr` / `gfx_rowbase` / `gfx_ls_box` frames *below* `gfx_points`, and
that is worth more than the frame it added above.

**And the 34-byte version is not distinguishable from HEAD here** — both read
164. docs/FIELD-NOTES.md 42.2.1 worried in as many words that
SPEC.md 5.6.9.3's first build cost its caller 34 bytes where the old routine
cost 26, *"and the reboot symptom appeared on that build"*. Measured, those 8
bytes never reach the maximum: **`gfx_points` is not the bottom of Cyclone's
deepest chain**, so a frame added at its entry is not on the critical path.
The worry was reasonable and it was wrong.

What the sweep does confirm is the reporter's own observation that
pre-inlining builds *"seemed to do it more often"*: **10 bytes of margin
against 28**, and a card asks for 16 before a tone is played.

## 7. Two harness faults, and both are the kind that pass quietly

- **P0 (`b9bb040`, before wave 5) cannot be run at all** — `tools/os88ui.py`
  did not exist yet, and hand-rolling clicks at remembered coordinates is
  exactly what that layer exists to stop. The point is dropped rather than
  faked.
- **The period ROM is gitignored, so a fresh worktree has none.**
  `os88marty.machine()` refused the IBM machine, correctly and loudly — which
  is what that refusal is for, and it would have silently run three points on
  GLaBIOS otherwise. Every worktree is given the same copy.

## 8. What this does not settle

- **The fix.** Nothing here proposes one. The candidates docs/FIELD-NOTES.md
  40.2 already lists are unchanged, and the honest framing is now *`cy_worker`
  runs at 85% of its class with no card in the machine*, which is a margin
  question and not only a driver question.
- **The 128 class.** `tests/unit/t_stkclass.py` reads `cy_worker` at 1.28x,
  the thinnest in the tree, since GFX-EMBEDDABLE-PLAN's wave 5 took it
  66 → 86 bytes. 164 of 192 measured is the same statement from the machine.

## 9. How to re-take it

```sh
python3 - <<'PY'   # or any script; the shape is what matters
# boot os8088_5150_herc_sb, ui.path("B:/GAMES/CYCLONE.O88"),
# m.key("Enter"), then hold:
#   m.key("ArrowRight", down=True, up=False)
#   m.key("Space",      down=True, up=False)
# ...and read stkwater.water() over sch_stacks each pass. Slot 4 going
# None IS the panic: the task record is gone.
PY
```

`os8088_5150_herc_sb` needs the period ROM, so it goes through
`os88marty.machine(name, why_ibm=…)`; without a reason it resolves to
`os8088_5150_herc_sb_gla` and the run is a different machine.

The box: 4-core Xeon @ 2.10 GHz, two MartyPC instances at a time.
