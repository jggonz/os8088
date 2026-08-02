# os8088 sound plan

**Standing plan for the sound layer: multiple outputs, honest routing, speaker first.**
SPEC.md is the binding contract for what the kernel *is*; this document is the plan for
how os8088 gets sound — why each promise is shaped the way it is, what each device can
actually deliver on the floor machine, and what is deliberately not promised. Read this
before assuming the layer can mix, stream in the background without a Sound Blaster, or
touch a timer. Every interface named here lands in SPEC.md *before* its code (new §34
plus the amendments listed at the end).

Lens: every promise in the API is backed by a per-device budget on the floor machine
(IBM XT, 8088 @ 4.77 MHz ≈ 4,772,727 cycles/s). Where the floor cannot deliver, the API
says so at call time (CF/error return) and the Control Panel says so in prose — the
`bb_avail` idiom, three layers deep: the probe flag gates the setter AND the caption AND
the click. Cycle figures below are 8086-nominal; a real 8088's 8-bit bus and prefetch
stalls inflate them 20–40%, which is why every margin claim here is a *bound to be
validated* at its phase gate, not an established fact.

## The constraint

| Device | What it really costs on the floor | Verdict |
|---|---|---|
| Speaker tone (PIT ch2 mode 3) | 5 port writes to start, 3 to stop, **zero CPU while sounding** | Always-present floor tier |
| Speaker PCM (ch2 mode 0 PWM) | ~596 CPU cycles/sample budget at 8 kHz; a minimal per-sample ISR is 200–300 cycles (36–50% CPU) and IRQ0 cannot preempt the mouse ISR | **Interrupt-paced PCM is arithmetically dishonest on the floor. Busy-loop exclusive clips only.** |
| OPL2 FM (AdLib) | a note-on is 2 register writes ≈ 0.2–0.6 ms (the pinned counted-read delays, not the 52 µs chip minimum), no IRQ, no per-sample cost | The real background-music tier — ~10 notes/s is still <1% CPU even on the XT |
| SB DMA | DMA paces itself; 1 IRQ per block (2 KB @ 8 kHz = 256 ms) | The **only** self-clocked PCM; the only tier where "samples play while the GUI runs" is promised |
| SB direct (10h) / Covox LPT | CPU-paced, same math as speaker PCM | Same exclusive-clip contract, different `out` target |

Consequences baked into the abstraction:

- **No software mixing.** The router assigns one owner per (sink, tier). Mixing two PCM
  streams costs 20+ cycles/sample/stream — a fast-machine luxury that would need its own
  `bb_avail`-style gate. Out of scope for all five phases; the ownership contracts are
  shaped so a future mixer changes no caller.
- **PCM has two tiers with different contracts**, and a request states which it needs:
  `PCM_BG` (background, block/IRQ semantics — SB only) and `PCM_EXCL` (exclusive clip;
  the caller's task holds the CPU for the clip — speaker/Covox/SB-direct). A `PCM_BG`
  request on a speaker-only machine **fails cleanly** (AX=err); it does not silently
  freeze the desktop. Degradation is the caller's explicit second call.
- **PIT ch0 is never written.** Not re-rated, not re-moded, not "sped up and divided".
  The sample clock for speaker PCM is *read* from ch0 by latching — the same
  non-destructive operation `sch_account` already performs — under an IF=0 window so
  latch users can never interleave (§ speaker PWM below). The 65536 radix, the 0x8000
  pending-IRQ threshold, `sched_init`'s seed, `sch_cycles` units and `sched_unhook`'s
  restore are all untouched by construction.
- **Streams sourced from the floppy must be fully staged before playback.** Task
  switching pauses during floppy reads (`sch_lock`), so no refill task runs while
  int 13h is in flight. This is a pinned rule, not a footnote (SB section below).
- **The kernel owns stream pacing.** Packages may own at most one worker task
  (SPEC.md §20.6) and that worker may not call the stream verbs; they also cannot
  receive queue
  events (the event queue's only consumer is the UI task, and it discards everything
  but mouse events). So opening a background stream spawns a *transient kernel task*
  from the existing 12-slot pool that paces the buffer refills; packages observe
  progress by polling the status verb from their callbacks. There is no sound event
  type (see Rejected).

---

## Module / file layout

One subsystem per `.inc` (the SPEC §4 module table pattern), landing at their phases:

```
kernel/snd.inc      Phases 1–2: core (router, owner records, snd_tick, driver table),
                    speaker driver (tone + PWM clip player), API slot targets
kernel/sndfm.inc    Phase 3: OPL2 driver (opl_ prefix)
kernel/sndsb.inc    Phases 4–5: Sound Blaster driver (sbl_ prefix)
```

`%include`d after `ctrl.inc` in kernel/kernel.asm (`farcall.inc` already precedes all
far modules). `snd_` prefix on every core label, `opl_`/`sbl_` in the driver files —
one flat namespace. **Every file ends on `section .text`** (ctrl.inc precedent;
`-w+error` makes a violation a build failure).

| Piece | Section | Why |
|---|---|---|
| Driver table, caps/route bytes, owner records, tone core (ch2 mode-3 on/off, 61h RMW), `snd_beep`, `snd_tick`, router, all 5 API slot targets (stubs included), `snd_release_inst`, `snd_unhook` | `.text` | hot or ISR-adjacent; ISRs and their whole call path are barred from `.fartext` (SPEC §33) |
| Speaker PWM clip loop (`spk_pcm_run`) | `.text` | hot loop — barred from far |
| SB block-IRQ handler (`sbl_isr`), the IRQ-discovery candidate stubs (~30 B: record which vector fired, confirm, EOI, iret), DMA re-arm, DSP byte write/read with timeout, stream refill task body, `snd_rec_read`/`snd_stage` copies | `.text` (Phases 4–5) | ISRs + hot; the discovery stubs are interrupt handlers, so §33 bars them from far even though discovery itself is cold |
| OPL2 register-write pair helper (`opl_wr`, ≈40 B) | `.text` (Phase 3) | called at note rate from tasks/callbacks, and by `snd_tick` for routed-tone expiry |
| Probes (OPL2 timer dance, SB reset scan, SB IRQ-discovery *orchestration*), OPL2 init (~245 writes ≈ 25–70 ms) and patch loader, Control Panel Sound page paint/click, PWM xlat-table builder | `.fartext` via the existing FARSHIM/KCALL machinery | cold; far code keeps DS=KERNEL_SEG so all its *data* (strings, patch bytes, CP state) stays in `.text` |
| Small state (owner records, deadlines, stream bookkeeping, PWM debug counters) | `.bss` (~100 B); **everything `snd_tick` can read before `snd_init` runs is gated** (see boot wiring) | |
| PWM rescale table (256 B, rebuilt per clip rate) | `.bss` | DS-addressable for `xlat` |
| Sample staging + SB DMA double buffer + record ring | **`SND_SEG` = 0x3000, linear 0x30000–0x3FFFF** — new SPEC §2 row | the only free 64 KB block on the 256 KB floor (RAM ends at 0x40000); wholly inside 8237 physical page 3, so every buffer in it is 64 KB-page-crossing-safe by construction; reached via **ES only**, never DS |

No `.lowbss` use — only 4,094 B of guard slack exists there and it is task-stack
clearance, not a buffer pool. The stream refill task's stack comes from the existing
dynamic pool (it is a transient `task_spawn`, not a resident task).

### `SND_SEG` region map — pinned, because a 64 KB segment with one implicit owner is a bug factory

```
0x0000–0x0FFF   SB DMA double buffer, 2 × 2 KB          (kernel-owned, Phase 4)
0x1000–0x2FFF   SB record ring, 8 KB                    (kernel-owned, Phase 5)
0x3000–0xFFFF   staging pool, ~52 KB                    (granted to instances)
```

The staging pool has a tiny allocator: first-fit grants stamped with the owning
instance slot (the package-pool idiom — occupancy derived from the grant records, no
free list), allocated and freed through the STREAM slot's verbs, and force-released by
`snd_release_inst` at teardown. Packages never hold an ES pointer into `SND_SEG`: data
goes in via the stage verb (kernel copies caller→grant) and comes out via the read verb
(kernel copies grant→caller) — the `dsk_get_dir` staging idiom in both directions. SB
playback DMA never runs from a grant: the refill task copies grant→double buffer, so
the DMA contract is satisfied by construction, not by caller discipline.

**Interaction with docs/MEMORY-PLAN.md Step D, decided now**: Step D (packages into
their own segments) must carve its per-package segments from **0x20000–0x2FFFF only**
(shared with the menu save-unders, whose extent the §2 amendment pins to that block).
`SND_SEG` keeps 0x30000–0x3FFFF on the 256 KB floor; on bigger machines Step D can
range above 0x40000/BB_SEG instead. Recorded in MEMORY-PLAN.md so the conflict is
settled before Step D starts, not discovered mid-migration.

## Driver abstraction

### Capability bits (published in `snd_caps`: one byte per driver row + a merged word)

```
SND_CAP_TONE     equ 01h   ; square voice(s): freq on/off
SND_CAP_FM       equ 02h   ; 9-ch 2-op patch + note-on/off (OPL2 model)
SND_CAP_PCM_BG   equ 04h   ; self-clocked block PCM out (DMA+IRQ) - background-safe
SND_CAP_PCM_EXCL equ 08h   ; CPU-paced PCM out - exclusive clip contract
SND_CAP_PCM_IN   equ 10h   ; block PCM input (recording)
```

Wire format for all PCM, pinned: **8-bit unsigned mono** — native on SB/Covox, cheap
per-rate rescale for speaker PWM (once per clip via a 256-byte xlat table, never a
per-sample multiply).

### Driver table (`snd_drv`, `.text`, stride SDRV_SIZE = 16 — the `inst_kinds` idiom)

```
SDRV_CAPS   equ 0   ; db  capability bits the hardware class supports
SDRV_FLAGS  equ 1   ; db  bit0 = present (probe result; published LAST, bb_avail idiom)
SDRV_TONE   equ 2   ; dw  near ptr: tone op
SDRV_NOTE   equ 4   ; dw  near ptr: FM op        (0 if unsupported)
SDRV_PCM    equ 6   ; dw  near ptr: PCM-out op   (0 if unsupported)
SDRV_PCMIN  equ 8   ; dw  near ptr: PCM-in op    (0 if unsupported)
SDRV_PROBE  equ 10  ; dw  near ptr to the probe's FARSHIM stub in .text
                    ;     (0 = unconditionally present) - SPEC §33.3 rule 3: near
                    ;     pointers reach far code only through shims; KCALL/FARK is
                    ;     the reverse direction (probe bodies calling kernel helpers)
SDRV_NAME   equ 12  ; dw  .text string ptr ("Speaker","AdLib","Sound Blaster")
            equ 14  ; dw  reserved (SB: packed base/IRQ/DMA config word)
```

Rows: 0 = speaker (present=1 as initialised data — always there), 1 = OPL2, 2 = SB.
A future Covox row is a fourth entry with `SDRV_PROBE=0` and presence set from a Control
Panel checkbox — announced, not detected (it is undetectable resistors). The op
pointers (`SDRV_TONE`..`SDRV_PCMIN`) are plain `.text` routines, never far.

### Route selection: per-tier preference lists

Route policy is data, not code: per-tier preference lists in `.text`
(`snd_pref_tone db DRV_OPL, DRV_SPK, 0FFh`, `snd_pref_pcm db DRV_SB, DRV_SPK, 0FFh`, …)
— the first *present* driver wins. One override byte per tier (`snd_route[]`, default
0FFh = auto, initialised `.text`) lets the Control Panel pin a tier to a specific sink,
checked before the list; a route change to an absent driver is refused at three layers
(setter, caption, click). Adding an SN76489 or Covox row later is a table row plus a
list entry — zero router code changes.

### Register contracts for the ops

SPEC prose style; every op preserves all registers except documented outputs; callable
from task context only unless stated.

**Tone op** — `AX` = frequency in Hz (19..20000), or 0 = off. `DL` = voice (0 for
speaker; 0–2 reserved for a future SN76489 row). Out: CF=0 ok, CF=1 unsupported
voice/freq. Speaker body: divisor = 1193182/AX via `div`; `pushf/cli…popf` around the
43h/42h/42h/61h-RMW quad; 61h upper bits preserved (keyboard/parity logic on the XT).

**FM op** — `AL` = verb: 0 note-on (`CL`=channel 0–8, `BX`=freq Hz — F-Number/block
math done inside: F-Number = Hz·2^(20−block)/49716, block chosen so F fits 10 bits),
1 note-off (`CL`), 2 patch-load (`CL`, `DS:SI` → 11-byte patch: 5 operator regs × 2 ops
+ C0h), 3 all-off. Out: CF=1 if no FM sink. Atomicity, sized honestly: `pushf/cli`
covers only the address select at 388h, its 6 counted status reads, and the data write
at 389h; the 35-read post-data delay runs at **the caller's IF** (the address register
is stable, and same-tier interleaving is excluded by the router's ownership, not by
cli). A register write is ~430–1,300 cycles ≈ 90–280 µs all-in on the floor, of which
≤ ~100 µs is `opl_wr`'s own IF=0 window from task context; a note-on (2 writes) is
~0.2–0.6 ms. Two callers run whole writes at IF=0: `snd_tick`'s sanctioned key-off, and
the §34.3 grant window — an OPL2-routed tone grant is one ~0.6–0.7 ms IF=0 stretch on
the floor, the accepted cost of the binding single-window grant rule (SPEC §34.1).

**PCM-out op** — `AL` = verb: 0 start (`ES:SI` buf, `CX` len, `DX` rate Hz),
1 stop, 2 feed/status. Exclusive drivers (speaker) implement verb 0 as
*run-to-completion* (returns when the clip ends or aborts); background drivers (SB)
return immediately and interrupt per block, and their verb-0 data source is the kernel
double buffer only (the refill task feeds it — callers never hand SB a raw pointer).
This asymmetry is deliberately **not** papered over — it is the hardware truth, and the
public API exposes it as two different calls.

**PCM-in op** — SB only; verbs mirror PCM-out into the record ring; half-duplex with
PCM-out enforced by the router, not the driver.

**Probe** (far body behind the `.text` FARSHIM, boot-time): out `AL`=1 present
(+config in the reserved word), 0 absent. Order: speaker (none needed) → OPL2
timer-flag dance at 388h (~200 µs: mask/reset via 04h←60h/80h, read s1; timer-1 FFh +
start 21h; wait ≥80 µs by counted status reads; read s2; present iff
`(s1 & E0h)==00h && (s2 & E0h)==C0h`; clean up 04h←60h/80h) → SB reset scan over bases
{220h,240h,210h,230h,250h,260h} with a **10 ms poll timeout per base** (an absent bus
floats FFh; the ~100 ms allowance is only for a present-but-slow DSP, retried once on
220h): write 1→2x6h, ≥3 µs, write 0, poll 2xEh bit 7, read 2xAh == 0AAh → SB version
via E1h (auto-init strategy gate at ≥ 2.00) → SB IRQ discovery **deferred to first
use**: hook candidates {7,5,3,2} onto the `.text` discovery stubs, issue F2h
(trigger-8-bit-IRQ, supported by all DSPs), keep the vector that fires **only after
the stub's 2xEh bit-7 read confirms the DSP asserted it** (defeats a coincidental
spurious IRQ 7 during the window; retry F2h once if the confirm fails), unhook the
losers. All presence flags published **last** after each device is fully configured
(SPEC §29.2 publish-last idiom).

## Routing model

- **Tone tier**: one logical channel, single owner. Owner record = {instance byte,
  priority byte, generation byte, expiry ticks}. Steal policy: a new request with
  priority ≥ the current owner's takes the channel (kernel UI beeps use priority 0C0h;
  package default 040h); lower priority is refused CF=1. Tone-off (AX=0) obeys the same
  compare, so background music cannot silence an alert. Duration-limited tones
  (`CX` ticks ≠ 0) self-expire via `snd_tick` — no task needed — and the expiry is
  **generation-guarded**: `snd_tick` silences only if the owner generation still matches
  the one stamped at grant. Route: speaker by default; Control Panel can route tones to
  OPL2 (square-ish patch on channel 8) to free the speaker — and **whenever OPL2 is in
  the tone route or preference list, channel 8 is reserved out of the FM allocator's
  bitmap**, so a package grabbing all FM channels cannot steal the tone channel.
- **Grant atomicity, pinned**: every grant, steal, and release updates its owner record
  (generation, priority, expiry) *and* its ports inside a **single**
  `pushf/cli…popf` window. `snd_tick` runs at IF=0 between any two task-context
  instructions; without this rule a tick landing between the generation stamp and the
  expiry store sees new-generation-with-stale-expiry and can silence a just-granted
  tone. The generation guard is only sound because task-side writers are atomic
  w.r.t. the tick. Same rule covers the PWM steal path (`snd_ch2mode` + generation +
  silence are one unit).
- **FM tier**: 9 channels (8 when the tone reservation is active), claimed per
  requester on first touch of a caller-named channel (bitmap + owner stamps — no
  allocator picks channels); channel handles are the raw 0–8 index.
- **PCM_EXCL**: whole-machine resource, one clip at a time, no queue — a second caller
  gets busy. **Refused (AX=1 busy) while any PCM_BG stream is open**: an exclusive clip
  raises `sch_lock` for its whole duration, which would freeze the SB refill task and
  force the stream through its underrun path — the router refuses the combination
  instead of shipping the surprise.
- **PCM_BG / PCM_IN**: per-card owner record {instance, generation}; in and out are
  **mutually exclusive on one SB** (same DSP + DMA channel) — the router models
  half-duplex, the driver never sees the conflict. Stream handles carry the generation
  byte: feed/close/status on a stale handle return "stale" instead of acting on a
  reused stream.
- **Instance teardown releases everything** (`snd_release_inst`, in: AL = instance
  slot): every grant — tone ownership, FM channel bits, stream ownership, staging
  grants — is stamped with the owner instance at the same dispatch sites that bill
  `I_CYC`, and `snd_release_inst` force-releases all of them (closing a live stream
  ends its refill task) from the existing teardown window (`app_close_win` synchronous
  path and the `inst_task_die` die-flag path). A closed package can never leave a tone
  droning or a dangling SB stream. Pinned as a SPEC §29 amendment.
- **Who mixes: nobody.** Simultaneity = different tiers on different sinks (FM music on
  OPL2 + tone beep on speaker + SB stream all coexist, minus the PCM_EXCL/PCM_BG
  exclusion above). Same-tier contention = ownership + priority steal.

## PC speaker (the crux)

### Tones — Phase 1

Ch2 mode 3 (`0B6h → 43h`, divisor lo/hi → 42h, 61h |= 03h), off = 61h &= 0FCh. All 61h
access is read-modify-write inside `pushf/cli…popf` (the upper bits belong to the XT's
keyboard/parity logic). Ch2 mode is a one-owner state machine (`snd_ch2mode`: 0 free /
1 tone / 2 PWM) — tone and PWM cannot coexist and the router serialises them (PWM start
steals + silences a lower-priority tone; a tone during a clip is refused CF=1).
`snd_init` saves the boot state of 61h bits 0–1; `snd_unhook` restores it.

### Sampled playback — PWM one-shots paced off ch0 latch reads (Phase 2)

**Modulator**: ch2 **mode 0, lobyte-only** (`90h → 43h` once at clip start), 61h bits
0–1 held high. One `out 42h, AL` per sample emits a low pulse of AL PIT-cycles; the
speaker cone integrates the pulse train; the sample rate is the carrier. Restore
mode-3-idle + gate low at clip end.

**Pacer — coexistence with the scheduler, exactly**: the scheduler owns ch0 (mode 2,
divisor 65536) and `sch_account` computes `now = ticks·65536 + (65536 − latched count)`.
The clip loop *reads the same clock*: latch ch0 (`0 → 43h`), read two bytes from 40h,
negate → phase, a free-running 16-bit value incrementing at 1,193,182 Hz. Ch0 is
**never written**. One new pinned rule makes the sharing safe: **every ch0 latch+read
triple — that is, every `sch_account` call site (`sch_isr`, `task_cycles`,
`task_debit`, the yield/exit paths) and the PWM pacer — runs inside one
`pushf/cli…popf` window.** The 8254 ignores a second latch command while a latched
value is unread, so an interleave would feed one reader a stale byte; the IF=0 window
makes interleaving impossible in both directions. Every *existing* site already
complies (each wraps its latch at IF=0 today) — the rule codifies current practice so
the invariant is auditable, and it is pinned in SPEC §34, not left as folklore.

**The loop** (SI=samples, CX=count, BX=xlat table, BP=N cycles/sample, DI=deadline):

```
        ; per clip: far code builds the 256-byte xlat table t[s] = 1 + s*(N-2)/255 (~8 ms)
        ; and latches snd_btn0 = [mouse_btn]  (click-to-abort baseline)
.next:  lodsb                     ; 16 cycles   (ES override when clip is in SND_SEG)
        xlat                      ; 11          AL -> 1..N-1, pre-clamped
.wait:  pushf                     ;  } one atomic ch0 latch+read triple
        cli                       ;  }
        ; save sample, latch ch0 (0 -> 43h), in al,40h twice, reassemble via DX
        ; (~110 cycles per poll all-in)
        popf
        mov  ax, dx               ; DX = phase
        sub  ax, di               ; wrap-safe signed: phase - deadline
        js   .wait                ; early -> poll again (IF=1 between polls)
        cmp  ax, bp               ; late by more than one period?
        jbe  .emit
        mov  di, dx               ; RESYNC: drop samples, never burst catch-up writes
        inc  word [snd_pcm_resync]; debug counter, the Phase 2 floor gate reads it
.emit:  out  42h, al              ; 14 - the pulse (sample restored to AL first)
        add  di, bp               ; next deadline
        ; abort checks, on the emit path only (~45 cycles):
        ;   mov al,[mouse_btn] / and [snd_btn0],al   <- FOLD RELEASES INTO THE BASELINE
        ;   then a bit set in AL but not in snd_btn0 -> exit CF=1 (click-to-skip)
        ;   [snd_abort] set (snd_stop / release path) -> exit CF=1
        loop .next
```

**Cycle math at the default rate**: N = 149 → carrier 1,193,182/149 = **8,008 Hz**;
depth = log2(149) ≈ **7.2 bits**. Budget: 4,772,727/8,008 ≈ **596 CPU cycles/sample**.
Fixed work (lodsb+xlat+out+add+abort-fold+checks+loop, with 8088 fetch stalls) ≈
**120–135**; each poll ≈ 110; typically 3–4 polls/sample → ~450–575 total: it fits,
with little to spare — which is precisely why this is a busy loop and not an ISR, and
why the figure is a *bound*: the Phase 2 gate measures the resync counter on 86Box and,
if the drop rate at N=149 is material on the floor, the default rate drops to ~6 kHz
(the SPEC records the measured figure). Rate support: the computed N must land in
**74..255** (mode-0 lobyte-only ⇒ N ≤ 255 ⇒ rate ≥ 4,679 Hz; N ≥ 74 ⇒ rate ≤ 16,124 Hz)
— a rate whose N falls outside returns **err 2, never a silent clamp** (a 22 kHz clip
"clamped" to N=74 would play 27% slow and flat; refusal is honest, resampling is the
caller's job). One more honesty line, pinned: above ~12 kHz the ±1-poll granularity
(~23–27 µs) exceeds a third of the sample period, so quality is jitter-dominated below
the nominal depth. Jitter: ±1 poll plus the surviving IF=0 stretches; the resync rule
turns a long interrupt into dropped samples, never a buzz burst. Documented audible
truth: 8 kHz carrier whine + telephone-grade audio; that *is* speaker PCM.

**Abort — a click always skips the clip.** At clip start the loop latches
`snd_btn0 = [mouse_btn]`; on each emitted sample it first **folds releases into the
baseline** (`mov al,[mouse_btn]` / `and [snd_btn0], al`) and then tests for a bit set
now but clear in the baseline. The fold is what makes the canonical case work: clips
start from W_ONCLICK handlers — dispatched on EVT_MDOWN with the button *still held* —
so the baseline starts with bit 0 set; the release retires it, and the next press
differs from the baseline and aborts. (Without the fold, no left click could ever
abort a click-launched clip.) `snd_abort` (set by `snd_stop` and `snd_release_inst`)
is checked in the same window. On click-abort the kernel **drains the aborting
EVT_MDOWN (and its EVT_MUP) from the event queue** before returning err 5, so the
skip gesture cannot fire a menu, close box, or icon under the cursor. No code is added
to `mou_isr` — the checks are byte loads on the emit path; the mouse module stays
sound-free.

**Scheduling contract**: `spk_pcm_run` executes on the **caller's task** with IF=1
throughout, wrapped in `inc byte [sch_lock] … dec byte [sch_lock]` — the *same*
contract as `disk_read`'s int 13h window (SPEC §7 already defines it: ticks advance,
the BIOS chain feeds the floppy motor, `sch_account` runs, sleepers mark ready; only
involuntary switching pauses). The mouse ISR keeps `mouse_x/y/btn` fresh throughout —
but the cursor only *moves* if the caller does not hold the gfx lock; from window
callbacks (which run under `gfx_lock`, the normal trigger) it is frozen for the clip,
which also shrinks the mouse ISR's worst IF=0 stretch to packet decode. Pinned in SPEC
§34 as the second sanctioned `sch_lock` raiser, with the meaning it has today — not a
new one. Cap: CX ≤ 65535 samples (≈ 8 s at 8 kHz); SPEC recommends callers chunk at
≤ 2 s, and the click-abort bounds the user's worst case regardless. The desktop freezes
for the clip; that is disclosed in the API (`PCM_EXCL`), in the Control Panel caption,
and gated by a user policy switch (`snd_excl_ok`, default on, CP-flippable) —
`osapi_snd_play` returns err 3 when the user has disabled it.

## `snd_tick` — the one scheduler touch-point

A leaf called from `sch_isr` **immediately after `call sch_account`** — the exact
precedent: called (not inserted in the `sch_isr`→`sch_switch`→`sch_resume`
fall-through), runs every tick, both modes, even while floppy reads hold `sch_lock`.
Contract, pinned in SPEC §8.2's order list (chain BIOS → ticks++ → sch_account →
**snd_tick** → wake scan → lock test): entered at IF=0, DS=KERNEL_SEG already, full
frame saved so AX/CX/DX are free, never touches the switch path, never EOIs.

**Boot gate — the garbage-state rule.** `sched_init` hooks int 08h as kmain's second
act; `snd_init` runs seconds of ticks later, and SPEC §8 pins that `.bss` is *not*
cleared at boot. So `snd_tick`'s first instruction tests **`snd_live`** — a `db 0` in
initialised `.text` data (the `osapi_seed` idiom), set to 1 as `snd_init`'s **last**
act (publish-last) — and returns while it is clear. Pinned in §34.7: every byte
`snd_tick` reads must have a defined value from the instant int 08h is hooked, i.e.
either be initialised `.text` data or sit behind the `snd_live` gate.

Duties once live: ~30 cycles when idle (`cmp byte [snd_live+pend],0 / jz / ret`);
decrement the tone-expiry counter and, at zero with a matching generation, silence the
tone on its route — ch2 (3 port ops) on the speaker route, or **one sanctioned OPL2
key-off** (a single B0h register write via `opl_wr`, ~90–280 µs, at most once per tone
end at 18.2 Hz) on the OPL route. That is the pinned worst case for the tick ISR; it
is bounded, rare, and honest — the alternative (a deferred pend flag) has unbounded
latency and lets a routed tone drone. Phase 4 adds the SB stream watchdog (a block-IRQ
that failed to arrive within 2× the block period stops the stream instead of hanging
the owner).

## Kernel API additions

Jump table, append-only from 0x0078. **All five slots ship in Phase 1** — the FM and
STREAM targets are 3-byte stubs returning the error path (CF=1 / AX=4 no-sink) until
their phases land. One assertion bump, once: `26 * 4` → `31 * 4` in kernel/kernel.asm
(the literal at kernel.asm:158), SPEC §20.3's pinned table and `apps/os88api.inc`'s
`OSAPI_*` equs updated in the same change. A package built against the full ABI never
jumps past the table end on any kernel from Phase 1 on.

```
OSAPI_SND_CAPS   equ 0x0078  ; out AX = merged caps word, BL = tone route (0 spk, 1 OPL),
                             ; DX = present-driver bits
OSAPI_SND_TONE   equ 0x007C  ; AX=freq Hz (0=off), CX=duration ticks (0=until off),
                             ; DL=priority; out CF=1 refused (higher-priority owner);
                             ; AL=owner generation on success (matches STAT queries)
OSAPI_SND_PLAY   equ 0x0080  ; PCM_EXCL clip: ES:SI=8-bit unsigned mono, CX=count,
                             ; DX=rate Hz (N=1193182/rate must land in 74..255);
                             ; BLOCKS for the clip; a mouse click aborts it.
                             ; out AX=0 ok / 1 busy (incl. PCM_BG stream open) /
                             ; 2 rate / 3 disabled-by-user / 4 no sink / 5 aborted.
                             ; ES restored per SPEC §1.
OSAPI_SND_FM     equ 0x0084  ; AL=verb 0 on/1 off/2 patch/3 all-off, CL=ch, BX=Hz,
                             ; DS:SI=patch(11B); out CF=1 no FM sink     (live Phase 3)
OSAPI_SND_STREAM equ 0x0088  ; PCM_BG/PCM_IN + staging, verbs below     (live Phases 4-5)
```

`OSAPI_SND_PLAY` takes **ES:SI** (not DS:SI) so a clip staged in `SND_SEG` plays in
place without copying into the shared kernel segment; packages that keep clips in
their own image just set ES=DS. (The busy loop is CPU-paced — no DMA constraint — so
an arbitrary caller buffer is legal here, unlike the SB path.)

### `OSAPI_SND_STREAM` verbs — the staging + stream surface

| verb | name | contract |
|---|---|---|
| 0 | open-out | `DX`=rate, `SI`=grant offset, `CX`=valid bytes staged so far; spawns the kernel refill task; out `AH`=handle, `AX`=err (6 = no task slot) |
| 1 | feed | `AH`=handle, `CX`=new total valid length — extends a progressively-staged stream |
| 2 | close | `AH`=handle; ends the refill task, frees the stream record |
| 3 | status | `AH`=handle; out `AX`=state (playing / underrun-paused / ended / stale), `DX`=bytes consumed — **this poll is the notification mechanism**; callbacks check it, there are no sound events |
| 4 | open-in | `DX`=rate (up to the input ceiling), `SI`=grant offset, `CX`=capture capacity; a kernel task drains the record ring into the caller's grant until CX bytes land, then stops the DSP; out `AH`=handle. Half-duplex with open-out is err 1 by the shared owner record |
| 5 | read | copy `CX` bytes from the grant at offset `SI` into caller `DS:DI` (kernel-staged copy — verb 6's exact mirror; no handle, so captured data outlives its stream until the grant is freed) |
| 6 | stage | copy `CX` bytes from caller `DS:SI` into the grant at offset `DI` (kernel-staged copy — callers never touch ES=SND_SEG) |
| 7 | grant | `AL2`(sub-op) alloc/free: `CX`=bytes → out `SI`=grant offset, or free; stamped with the calling instance, force-freed by `snd_release_inst` |

**Execution model, pinned (this is what makes the SB phases usable at all)**: packages
may own one worker task but may not call the stream verbs from it (SPEC.md §20.3),
so verb 0/4 spawn a **transient
kernel task** from the existing 12-slot pool (the Clock/Bounce spawn idiom — no
resident sound task, no new `.lowbss` stack reservation). The task copies
grant→double-buffer halves as `sbl_isr` flags them consumed (or ring→grant for
recording), and exits at stream end/close/teardown. The package's job is: grant →
stage → open → poll status from its callbacks. Pool-full at open is a clean err 6.

What packages get: Minesweeper explosion = one `call OSAPI_SND_TONE` (`.o88` reloc
class 1 handles it); Notepad key click = 2 ms 2 kHz tone; a future music player = FM
notes via 0x0084, or staged PCM via 0x0088 on an SB machine — and on a speaker-only
machine it *asks* `OSAPI_SND_CAPS` first and offers the user the exclusive-clip
trade-off instead of pretending. (A file-read-into-grant OSAPI call — the missing
piece for playing clips *from disk* a package didn't ship with — is future work noted
in §34, not promised here; today a package stages data it generated or carries in its
image, which its ~19 KB pool budget bounds.)

Kernel-internal: `snd_beep` (no args, priority 0C0h, 3-tick 880 Hz through the active
tone route) for UI use — menu error, refused clicks.

## Sound Blaster (Phases 4–5) — and the floppy truth

- **Init** follows mouse_init's order verbatim: hook the discovered IRQ's vector (save
  old, install under pushf/cli) while masked at the 8259 → program/verify the DSP →
  unmask; mirror-image unhook joins `snd_unhook`.
- **`sbl_isr`** (`.text`, mou_isr discipline: push used regs + DS + ES, DS=KERNEL_SEG,
  `cld`, IF=0 throughout, no BIOS, never `sti`, own EOI `out 20h, 20h`): the
  **IRQ-7 spurious guard** — treat the interrupt as SB only if `snd_sb_expect` is set
  AND 2xEh bit 7 confirms; never EOI a spurious IRQ 7, just iret. ACK by reading 2xEh;
  single-cycle mode (DSP < 2.00): re-program DMA ch1 + command 14h for the other half
  of the double buffer (~40 instructions — the audible-gap mitigation on 1.x);
  auto-init (DSP ≥ 2.00, chosen at detect): nothing to re-arm; flag the consumed half
  for the refill task; EOI. Never touches VGA, gfx_lock, or the switch path.
- **DMA**: channel 1 only — **never ch2, the floppy's**. Mask 0Ah=05h, then for each
  16-bit value **clear the flip-flop via 0Ch and write two successive bytes (lo, hi)**:
  offset → 02h/02h, (len−1) → 03h/03h; page 3 → 83h; mode 49h/59h out (45h/55h in);
  unmask 0Ah=01h. (Nothing is multiplied by 2 — address doubling is a 16-bit-channel
  concept that does not exist on the XT.) Double buffer 2×2 KB at SND_SEG:0 — wholly
  inside physical page 3, page-crossing-safe by construction. Rates quantised via
  TC = 256 − 1e6/rate; ceilings honored (out ≤ ~22 kHz; in ≤ 13 kHz on 1.x / 15 kHz on
  2.0 normal mode). Every DSP poll (2xCh busy, 2xEh ready) carries a timeout.
- **Floppy coexistence, stated honestly**: the DMA and `sbl_isr` keep running during
  int 13h windows (`sch_lock` does not mask interrupts), but the **refill task does
  not** — task switching pauses, so after the double buffer drains (~512 ms at 8 kHz)
  a stream fed live from disk *will* underrun. Therefore, pinned rules: (a) **streams
  must be fully staged into their `SND_SEG` grant before playback starts** (verb 0
  checks CX covers the clip, or the caller accepts progressive-feed risk explicitly);
  (b) on underrun — `sbl_isr` finds the next half not refilled — the ISR pauses output
  (D0h, bounded write-poll), bumps `snd_sb_under`, and marks the stream
  underrun-paused (visible via verb 3); the refill task resumes with D4h after
  catching up, or the owner closes. Bounded silence + a visible status, never stale
  audio looping and never a wedge. The `snd_tick` watchdog covers the complementary
  failure (block IRQs stop arriving entirely).
- **Recording**: verbs 4–5 → 24h single-cycle or 2Ch auto-init into the `SND_SEG`
  record ring; half-duplex enforced by the router's owner record; consumers read via
  the verb-5 staging copy.

## AdLib / OPL2 (Phase 3)

`opl_wr` (`.text`, ≈40 B): AH = register, AL = value; select via 388h, 6 counted status
reads, write 389h — that much under one `pushf/cli…popf` — then 35 counted status reads
at the caller's IF. Real cost on the floor: ~430–1,300 cycles ≈ **90–280 µs per register write**
(an `in al,dx` from ISA is ~2–6 µs depending on loop structure — the 52 µs "datasheet"
figure is the chip minimum, not this implementation). Init (far, once, on probe
success): zero regs 01h–F5h — ~245 writes ≈ **25–70 ms**, fine in cold boot-time far
code, never from ISR context — then 01h←20h (waveform-select enable), BDh←0. Channel n
(0–8) has modulator operator offset {00,01,02,08,09,0A,10,11,12}[n], carrier =
modulator+3; note-on = A0h (F-Number low) then B0h (KEY-ON | block | F-Number high).
FM is fire-and-forget — a note costs ~0.2–0.6 ms once, then zero CPU while it sounds:
background music under full multitasking even on the floor, which is why the
preference list routes tones to OPL2 when present.

## Memory

Against measured headroom (guard 1 text+bss **27,565 B** free; guard 2 text+fartext
**26,780 B** free; `.lowbss` slack 4,094 B — §15.1 recipe, measured 2026-07-29).
Per-phase figures are **estimates to be re-measured at each phase boundary with the
§15.1 recipe** — Phase 1 in particular is ownership-and-table machinery whose
comparables (instance.inc) run bigger than a first sketch suggests:

| Phase | .text | .bss | .fartext | elsewhere |
|---|---|---|---|---|
| P1 tone core + router + all 5 slots (2 live, 3 stubs) + snd_tick + release/unhook | ~1.3 KB | ~48 B | ~250 B | — |
| P2 PWM player + play slot live + CP Sound page | ~600 B | ~310 B (256 B xlat + state + counters) | ~950 B | SND_SEG claimed (0 kernel cost) |
| P3 OPL2 (opl_wr + FM op; ~200 B patch data counted in .text) | ~300 B | ~16 B | ~1,300 B | — |
| P4 SB (ISR + discovery stubs + DMA + stream core + refill task body) | ~1.0 KB | ~64 B | ~900 B | 2×2 KB DMA double buffer at SND_SEG:0 |
| P5 recording + staging copies | ~300 B | ~8 B | ~300 B | record ring + grants share SND_SEG |
| **Total** | **~3.5 KB** | **~0.45 KB** | **~3.7 KB** | |

Phase 1 measured at its gate (§15.1 recipe, 2026-07-29): **861 B .text, 13 B
.bss, 0 B .fartext** — under the estimate, and the .fartext figure is zero by
construction, not luck: nothing §34.7 assigns to far code exists yet (the
speaker needs no probe; the OPL2/SB probes land with their phases), and
`snd_init` itself is a `.text` routine like every other kmain init. Guard 1
now 26,691 B free, guard 2 25,919 B free.

Phase 2 measured at its gate (§15.1 recipe, 2026-07-29): totals now
**15,893 B .text, 3,561 B .bss, 4,828 B .fartext** — guard 1 **25,602 B
free**, guard 2 **24,335 B free** (Phase 2 cost ≈ 1,089 B against guard 1,
≈ 1,584 B against guard 2 — within the ~600/~310/~950 estimate row).
Counters in QEMU at N = 149: a full 12,000-sample Test clip reads
**E:12000 R:2–4**; a mid-clip click-abort at ~0.55 s reads E:4389 with the
page quiescent after (the drain held — no click-through). The **floor
gate** — the same counters read on 86Box at N = 149 — is still owed; the
default rate stays 8,008 Hz until that read says otherwise.

Phase 3 measured at its gate (§15.1 recipe, 2026-07-29): totals now
**16,562 B .text, 3,581 B .bss, 5,005 B .fartext** — guard 1 **24,913 B
free**, guard 2 **23,489 B free**. Phase 3 cost ≈ 689 B against guard 1,
≈ 846 B against guard 2 — the split ran opposite the ~300/~16/~1,300
estimate row (+669 .text, +177 .fartext): the ops, the channel allocator
and `opl_wr` are all §33-barred from far (ISR-adjacent or
pointer-dispatched), while the far half (probe + init loop + patch loader)
compresses to a few loops. Total within the row's sum. QEMU gate: the
probe's 200 counted status reads detect the emulated OPL2 (and a no-adlib
boot publishes absent); an 880 Hz beep routed to OPL2 via the CP radio
reads 880.0 Hz dominant in the wav capture and goes silent ~190 ms after
grant (3-tick expiry + release tail) — the `snd_tick` single-B0h key-off
— with `--exclusive` clean over the whole session; a scratch package
sustaining a 440+660 Hz chord from its W_ONCLICK went silent at
close-mid-chord (the `snd_release_inst` → `opl_release_inst` teardown
leg). Floor-gate items (real-XT probe timing by ear) ride with Phase 2's.

Phase 4 measured at its gate (§15.1 recipe, 2026-07-29): totals now
**18,432 B .text, 3,700 B .bss, 5,429 B .fartext** — guard 1 **22,924 B
free**, guard 2 **21,195 B free**. Phase 4 cost ≈ 1,989 B against guard 1,
≈ 2,294 B against guard 2 (+1,870 .text / +119 .bss / +424 .fartext) —
over the ~1.0 KB .text estimate (the grant allocator, the verb surface and
the Test button's move onto it were under-counted) but the far half
compressed, and the total is well inside the slack. QEMU gate
(`make test-snd SB16=1 TESTAPPS=build/sbtest.img`, DSP 4.05 → auto-init):
detect finds base 220h and versions via E1h; first open discovers IRQ 5
via F2h with the 2xEh-bit-7 stub confirm (QEMU's F2h does set the
read-buffer status, so the pinned confirm recipe holds there) and hooks
`sbl_isr`; a fully staged 2 s 1 kHz stream plays gap-free while a window
drags (wav dominant 1000.0 Hz, one contiguous burst; screendumps show the
move); exhaustion pauses with verb 3 reading underrun-paused and consumed
= 16,000; the 2,400-byte never-fed open pauses at 2,400 with **no stale
audio looping** (the wav burst is bounded); a verb-1 feed of 800 B resumes
(D4h) and re-pauses at 3,200; close-box teardown mid-stream force-closes
the stream and frees the grant (`snd_release_inst` leg); the spurious soak
(IRQ hooked, no stream, menus + clicks) leaves no wedge; and the
PCM_EXCL exclusion holds (CP Test during an open stream: counters stay 0,
no freeze). Regressions re-run: refused-close beep 880.0 Hz exclusive,
CP Sound Test E:12000 R:2 through the new verb-7 grant path, FMTEST patch
880.0 Hz under `ADLIB=1`, and an untouched boot captures nothing. Still
owed to 86Box/real hardware: the whole single-cycle (DSP < 2.00) branch —
`vm/xtsb` does not exist yet, and SPEC §34.5's pinned fallback (refuse
< 2.00) stands if it proves unmaintainable — plus the standing Phase 2/3
floor-gate items. One semantic pinned during the build (now in SPEC
§20.3/§34.5): data exhaustion reads **underrun-paused**, not "ended" —
the ABI carries no clip length, so the kernel refuses to guess "finished"
vs "starved"; "ended" is the watchdog stop. Assertion note for future
automation (the SB analogue of Phase 3's OPL2 release-tail note): QEMU's
wav backend, on `quit`, flushes a ~20 ms residual chunk of stream data at
the absolute end of the capture — after ~45 ms of silence — whenever an
SB stream sits underrun-paused at exit. `sndcheck --exclusive` then fails
("activity outside the burst") even though playback itself was contiguous
and bounded; assert those sessions with the burst-map method instead, or
close the stream before quitting.

Phase 5 measured at its gate (§15.1 recipe, 2026-07-29): totals now
**19,370 B .text, 3,702 B .bss, 5,429 B .fartext** — guard 1 **21,984 B
free**, guard 2 **20,257 B free**. Phase 5 cost ≈ 940 B against guard 1
(+938 .text / +2 .bss / +0 .fartext) — over the ~300 B estimate row for
the same reason Phase 3 inverted its split: everything recording adds
(the open-in body, the ISR input leg, the drain machinery, the read verb)
is §33-barred from far, and nothing new is cold. QEMU gate
(`make test-snd SB16=1 TESTAPPS=build/sbtest.img`, keys r/d/f/s/c on the
extended SBTEST): the verb 6 → verb 5 staging round-trip returns the
16-byte pattern intact (5:Y) with no stream, and again *while a capture
is live*; open-in reads g:12288/o:R with verb 3 at recording(0); **QEMU's
sb16 never starts input DMA against a wav audiodev** (no ADC IRQs ever
arrive), which makes the watchdog leg the deterministic automated test —
status flips to ended(2) after ~2 ring-half periods with captured = 0;
feed on the input stream refuses err 7; half-duplex holds in both
directions (open-in during an out-stream → 1, open-out during the record
→ 1, both leaving the open stream and its grant untouched); close →
stale on every later verb; close-box mid-record force-closes the stream
and frees every grant (memory-verified: `sbl_str_act` = 0, `sbl_gr_act[]`
all clear; a relaunch re-grants at the pool base). Regressions re-run on
the same build: the 2 s out-stream plays 1000.0 Hz dominant while a
window drags (the drag outline is on the screendump), exhaustion reads
underrun-paused at 16,000, the progressive open resumes on feed
(2,400 → 3,200), refused-close beep 880.0 Hz `--exclusive` clean on a
speaker-only boot, CP Sound Test E:12000 R:0 through the verb-7 grant
path, FMTEST 880.0 Hz under `ADLIB=1`. **One latent Phase 4 bug found and
fixed by this gate**: `sbl_grant_chk` did its bounds math in DX,
clobbering DH — its own instance operand — so any record that passed the
instance compare but failed the range checks poisoned the compare for
every record after it; with a single live grant (all Phase 4 ever held)
it was unreachable, with two (a stream's grant + a scratch grant) every
staging call refused err 7. The math now runs in DI and the SPEC's
grant_chk note records the rule. Still owed to 86Box/real hardware: the
entire live-capture data path (ring-half drain copies, the overrun
pause/resume, the capacity-full stop) — unreachable in QEMU because no
input IRQ ever fires — plus audio-in fidelity by ear, riding with the
standing Phase 2/3/4 floor-gate items.

The P5 review pass then hardened the drain data path (now in SPEC
§20.3/§34.6): the ring → grant copy is **teardown-fenced** — 512-byte
chunks, each one act+generation-verified `pushf`/`cli` unit — because the
drain WRITES grant bytes, and a close-box teardown preempting a
whole-half `rep movsb` (~21 ms on an 8088) would have resumed writing
ring data into freed, re-grantable pool memory (the refill mirror stays
un-fenced deliberately: its copy only *reads*, and a stale read at
teardown is a bounded audio glitch). `sbl_consumed` now advances after
each chunk lands, never before, so verb 3's captured count can never run
ahead of the bytes verb 5 can actually read. The open verbs'
busy-check → publish gap is pinned as UI-task serialization (SPEC §20.3
— a future background-task caller must claim the record under
`pushf`/`cli` first), and `mouse.py down`/`up` grew optional `X Y`
arguments (goto-then-press; anything else errors) after the bare-`down`
footgun cost this pass a retest cycle.

Post-phases change (2026-07-30): the CP Test clip is no longer the
12,000-sample 1 kHz sine — it is the Recorder demo's 1 s 400→800→400 Hz
sine sweep (8,000 samples, SPEC §31.4/§35, its own kernel-side copy of
the generator). The gate paragraphs above record what was measured when
the old clip was live; any regression re-run from here reads **E:8000**,
and a sndcheck dominant assertion on the Test clip must accept the whole
400–800 Hz band (a sweep has no single line).

Post-all-phases slack ≈ 21.5 KB (guard 1) / ≈ 19.8 KB (guard 2) — measured,
not estimated, now that all five phases are in. `.lowbss` untouched (the refill task's stack is a normal
dynamic spawn). SND_SEG folds into the Task Manager's RAM figure via the `KLOWFAR_KB`
accounting hook idiom. A 256 KB machine gets: tones, beeps, FM if an AdLib is present,
click-abortable exclusive-clip PCM, SND_SEG staging — everything except background
PCM, which its hardware cannot do; with no cards it boots and runs byte-identically to
today until the first sound call.

## Boot / teardown wiring

`snd_init` joins kmain's ordered sequence after `tm_init` (it needs `far_init` for its
far probes and `sched_init` for tick-based timeouts — both long since run): save 61h
boot bits, run probes cold, publish presence flags, and **set `snd_live` last** — the
gate that keeps `snd_tick` inert through the boot window when its `.bss` is still
garbage (nothing clears `.bss`; SPEC §8 pins that). Teardown joins the **only** exit
path: `sched_unhook` gains a `call snd_unhook` beside `mouse_unhook` — silence 61h
bits 0–1 back to the saved boot state, leave ch2 quiescent, and (if hooked) mask the
SB IRQ, reset the DSP, restore its vector. Boot stays clean: no instances, no tasks,
no sound.

## Phases — each shippable and testable alone

**Test harness (built in Phase 1, used by all)**: new `make test-snd` = `make test`
plus `-audiodev wav,id=snd,path=build/snd.wav -machine pcspk-audiodev=snd` (Phases 3–4
add `-device adlib,audiodev=snd` / `-device sb16,audiodev=snd`). New
`tools/sndcheck.py`: opens the WAV after QMP `quit` flushes it, asserts RMS > threshold
in a time window and FFT-dominant frequency within ±5% of expected (Goertzel scan,
stdlib only). Two QEMU realities, measured at the Phase 1 gate and absorbed by the
tool: (a) the wav backend leaves the RIFF/data size fields zero on exit, so the header
is parsed by hand; (b) the pcspk stream only runs while the speaker gate is on, so
file time is speaker-on time, not wall time — "silence before the click" is asserted
as *an empty capture on a no-input boot* (`--expect-silence`) plus *nothing outside
the expected burst* (`--exclusive`) on the click run, which is a strictly stronger
statement than a timeline window. Both floppy geometries rebuilt every phase (the
kernel image changes even when nothing sound-side ships on disk).

- **Phase 1 — tone core (speaker only).** snd.inc skeleton, ch2/61h ownership + mode
  state machine, `snd_beep`, `snd_tick` beside sch_account behind the `snd_live` gate,
  router tone tier with priority + generation-guarded expiry + single-window grant
  atomicity, **all five API slots** (0x0078/0x007C live; 0x0080/0x0084/0x0088 as error
  stubs; assertion 26→31 once), `snd_release_inst` wired into the §29 teardown window,
  `snd_unhook` on the reboot path. SPEC §34 + §2 + §7 + §8.2 + §20.3 + §29 amendments.
  Wire one audible consumer: the menu-refused-click beep.
  *Test*: `test-snd`; QMP-click a refused action; `sndcheck.py build/snd.wav 880`;
  assert silence-before (no boot noise — this is also the `snd_live` gate's test);
  `screendump` unchanged elsewhere.
- **Phase 2 — speaker PCM + Control Panel page.** `spk_pcm_run` (ch0-latch pacer,
  sch_lock window, resync rule, release-folding click-abort + event drain), far xlat
  builder, slot 0x0080 live, CP Sound page (route radio, excl checkbox, Test button),
  `snd_excl_ok` policy, `snd_pcm_emitted`/`snd_pcm_resync` debug counters.
  *Test*: the CP Test button synthesises a 1.5 s 1 kHz sine into SND_SEG and plays
  it at 8,000 Hz (N = 149) through slot 0x0080. **Measured QEMU reality (11.0.2,
  worse than "imperfectly")**: the pcspk backend emits *zero* frames while ch2 is
  in mode 0 — the wav capture stays empty for the whole clip, so no WAV assertion
  about the clip is possible in QEMU at all. The WAV harness instead proves the
  *edges*: `sndcheck 880 --exclusive` on a run that plays a clip and then triggers
  the refused-close beep asserts (a) ch2 came back to tone-idle after PWM — the
  mode-3 beep still sounds — and (b) nothing else, boot included, ever opened the
  speaker. PWM fidelity by ear is 86Box/real-hardware work (the floor gate).
  The counters are the load-bearing check: a screendump-readable field (CP page
  caption) shows emitted/resync counts and the test asserts emitted ≈ clip length,
  resync ≈ 0 in QEMU. **Floor gate**: the same counters read on `make xt` (86Box) —
  if resyncs are material at N=149 on the XT, the default rate drops to ~6 kHz and
  SPEC records the measured figure; fidelity signoff is by ear there. Mid-clip QMP
  `mouse_button 1` aborts — the counters caption shows the truncated emit count (the
  WAV cannot show truncation: it never held the clip) AND the screen shows
  no click-through action; after playback QMP-drive the mouse to prove the GUI
  resumed; a screendumped Clock window proves ticks weren't lost; CP page driven by
  `mouse.py` down/to/up, screendump-verified.
- **Phase 3 — AdLib/OPL2.** sndfm.inc: far probe (timer dance), far init/patch loader,
  `opl_wr` + FM op behind slot 0x0084, tone-route-to-OPL preference live in CP (with
  the channel-8 reservation), snd_tick's sanctioned key-off path, channel-bitmap
  allocator released by `snd_release_inst`. *Test*: `-device adlib`; QMP-drive a test
  package playing a known chord; sndcheck asserts the note fundamentals; a timed tone
  routed to OPL2 expires (the snd_tick key-off test); CP page shows "AdLib: yes"
  (screendump); a no-adlib boot still shows "no" and refuses the radio; closing the
  package mid-chord silences it (teardown test). The gate package is **committed**:
  `apps/fmtest` (built into the scratch image `build/fmtest.img`, mounted with
  `make test-snd ADLIB=1 TESTAPPS=build/fmtest.img` — never on the shipped apps
  disks, whose directory order is pinned). Its first click patch-loads a carrier
  MULT=2 voice through slot 0x0084 verb 2 and keys 440 Hz — sounding **880 Hz** iff
  the caller's DS:SI patch actually reached the chip — its second click adds 660 Hz
  (the chord), 'b' requests a 3-tick 880 Hz tone (the snd_tick expiry leg), and the
  close box mid-chord is the teardown leg. Assertion note for future automation: the
  OPL2 envelope release leaves ~50 ms of tail after a key-off/close (one 10 ms block
  was measured at rms 0.00515, marginally over sndcheck's default 0.005 silence
  floor) — allow that tail, or raise `--silence-floor`, before asserting silence at
  a close boundary.
- **Phase 4 — Sound Blaster output.** sndsb.inc: far detect (reset scan, E1h version,
  F2h IRQ discovery with confirm-and-retry via the `.text` stubs), `sbl_isr`
  (spurious-IRQ7 guard, 2xEh ACK, half-swap, refill flags, underrun-pause, EOI), DMA
  ch1 programming, 2×2 KB double buffer, auto-init vs single-cycle strategies, the
  kernel refill task, grant allocator + verbs 0–3 and 6–7 live, `snd_tick` stream
  watchdog, the staged-before-playback rule, the PCM_EXCL/PCM_BG exclusion.
  *Test (auto-init path, QEMU)*: `-device sb16` (reports DSP 4.x); stage 4 s of
  synthesised audio via verbs 7/6, stream it while QMP-dragging a window — sndcheck
  asserts continuous tone (no gaps > 20 ms), screendumps prove the GUI ran; underrun
  test: open with a short valid length and never feed — assert the pause + verb-3
  underrun status instead of looping stale audio; spurious-IRQ soak: boot with no
  stream, confirm no wedge. *Test (single-cycle path, 86Box)*: new `vm/xtsb` config —
  the XT with an **SB 1.5/2.0 card** (86Box emulates the originals; QEMU cannot
  exercise DSP < 2.00 at all) — with a scripted manual checklist: continuous tone,
  per-block gap audibility, underrun pause/resume. If the config proves unmaintainable,
  the fallback is pinned now: the version gate *refuses* DSP < 2.00 (caps bit stays
  clear) rather than shipping a permanently unverified branch.
- **Phase 5 — SB recording.** Verbs 4–5 live (24h/2Ch input, DMA modes 45h/55h),
  half-duplex router enforcement, record-ring drain task, staging reads.
  *Test*: QEMU audio input is weakly scriptable — the automated test asserts the state
  machine (open-in refused while an out-stream is open, stale-handle refusals, status
  transitions, watchdog stop) via a probe package + screendump; audio-in fidelity is a
  documented manual check on real hardware.

## SPEC.md impact (SPEC first, always, per change)

- **New §34 — the sound layer** (snd.inc / sndfm.inc / sndsb.inc): 34.1 port ownership
  (PIT ch2 + 61h single-owner with the mode state machine and boot-state restore;
  388h/389h with the real write costs and the split cli window; SB base/IRQ/DMA-1;
  **the ch0 latch-atomicity rule enumerating every latch site; the rejected
  ch0-re-rate alternative recorded**), 34.2 driver table + caps + ops register
  contracts + preference lists (probe = FARSHIM near ptr), 34.3 router / ownership /
  priority-steal / generations / single-window grant atomicity / channel-8 reservation
  / half-duplex / PCM_EXCL-vs-PCM_BG exclusion / `snd_release_inst`, 34.4 speaker PWM
  scheme (N range 74..255 with err-2-not-clamp, xlat rescale, resync rule + counters,
  release-folding click-abort + queue drain, sch_lock clip contract + cap + chunking
  guidance + the >12 kHz honesty note), 34.5 SB (ISR contract, DMA lo/hi recipe,
  discovery stubs in .text + confirm-and-retry, buffers, underrun contract, **the
  staged rule**, watchdog, refill-task model), 34.6 recording + staging verbs,
  34.7 state listing **including the `snd_live` boot-gate rule**.
- **§2 memory map**: SND_SEG row (linear 0x30000–0x3FFFF, ES-only) with the internal
  region map; **in the same amendment, pin SAVE_SEG's extent to 0x20000–0x2FFFF** (the
  bound that makes "SND_SEG is free" true forever); fold into the Task Manager RAM
  accounting.
- **§7**: second sanctioned `sch_lock` raiser (`spk_pcm_run`), same semantics as
  `disk_read`; cursor-frozen-under-gfx_lock noted.
- **§8.2**: `snd_tick` added to the pinned per-tick order (called leaf, after
  `sch_account`), with the `snd_live` gate and the sanctioned OPL key-off worst case.
- **§20.3**: five slots 0x0078–0x0088 with contracts + the STREAM verb table;
  assertion 26→31 once (Phase 1); mirror `OSAPI_*` equs in `apps/os88api.inc`.
- **§29**: `snd_release_inst` in the teardown window (both the synchronous and
  die-flag paths); grants and streams listed among what teardown must free.
- **§31**: new Sound page row — noting explicitly that the `cp_items` paint/click
  words are **`.fartext` offsets dispatched through the two existing
  `cp_paint`/`cp_onclick` FARSHIMs** (ctrl.inc:130–136); no new shims.
- **§4 module table**: snd.inc row (+ sndfm.inc/sndsb.inc at their phases); **§33**:
  the new FARSHIM/FARK entries listed.
- **docs/MEMORY-PLAN.md**: Step D carves from 0x20000–0x2FFFF on the floor (done in
  this plan's commit).

## Rejected

- **Re-rating PIT ch0 for sample pacing** (with a 1-in-N BIOS chain divider). Breaks
  §8.1's 65536 radix, the floppy motor countdown, and every tick-denominated constant;
  even fixed, a minimal per-sample ISR eats 36–50% of the floor CPU and still jitters
  at tick scale behind the mouse ISR. Recorded in SPEC §34 so it is never re-litigated.
- **Interrupt-paced speaker PCM.** Same arithmetic; nobody ever shipped multitasking +
  speaker PCM on a 4.77 MHz 8088, and os8088 does not pretend to.
- **A sound event type (`EVT_SND`).** The event queue has exactly one consumer — the
  UI task — which discards everything but mouse events and actively drains the queue
  mid-drag; tasks must never pop the shared queue (a second popper steals mouse
  events). Sound events would be pure queue pressure against 16 slots, silently eaten.
  Notification is the STREAM status verb, polled from callbacks; the kernel refill
  task is what acts on block completion.
- **Software mixing.** 20+ cycles/sample/stream; a fast-machine luxury behind a future
  `bb_avail`-style gate at most. The ownership contracts already leave room for it.
- **Buffers in `.bss` or `.lowbss`.** The kernel window is for code; `.lowbss`'s
  4,094 remaining bytes are task-stack clearance. SND_SEG costs nothing and exists on
  every machine.
- **Per-sample `mul` scaling.** The 256-byte xlat table saves ~70 cycles/sample and is
  non-destructive — the caller's clip buffer is never modified, so replay and re-rate
  work.
- **A resident sound task.** Tone expiry is a `snd_tick` leaf; stream refills are
  *transient* per-stream spawns from the existing pool that exit with their stream. A
  permanent task would cost a 1,536-byte `.lowbss` stack for mostly-idle work.
