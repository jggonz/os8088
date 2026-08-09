# Handoff — the Sound Blaster claim round, and the memory work it uncovered

State as of `elendilon` @ the merge that carries this file. Everything below
is merged, built and booted; nothing is left uncommitted.

It started as "can we halve the Sound Blaster's heap claim" and ended in the
memory manager, because two of the three real defects were there and the
sound work only made them visible.

**Read first:** SPEC.md §50.6.3 and §50.6.4. Between them they carry the two
kernel bugs, and the second is the one to internalise — *two defects whose
symptoms cancel*, where fixing either alone leaves the other looking like the
fix did not work.

---

## 1. What landed

| | |
|---|---|
| `sound: derive the SB block geometry` | six sites re-encoded `SBL_HALF`/`SBL_RHALF` as shift counts and precomputed watchdog dividends; three layout rules became `%error` guards |
| `sound: DMA claim 12KB → 8KB` | the RECORD ring's halves, not the playback half — see §2 |
| `sound: the staging pool asks in tiers` | 128KB machine: `g:----- o:8` → `g:00000 o:K` |
| `tracker 45.18` | ring chosen from free RAM (>64KB → 16KB + 6-half pre-roll; ≤64KB → 8KB + 3-half), reserved by ASKING |
| `memory 50.6` | `mem_avail` counted purgeable as occupied; `mem_claim_1` clobbered its own retry parameters |
| `memory 50.6.4` | the purgeable priority ladder |
| `wm 11.96.1` | Minesweeper, Piano and Solitaire opt into the raise cache |

Measured, all on cycle-accurate 5150s with XT mode confirmed off the card
(`[sbl_tc]` = 0x4B = 5,524 Hz):

- `tests/sbtest` 2.00 s at dominant 1000.0 Hz; underrun `st:1 c:02400` with
  0.30 s then silence; capture `o:R` `st:1 c:06000`. Five framebuffer captures
  **byte-identical** across the 12KB→8KB change.
- 640KB + a 116KB module: ring 16384, lead min 7.00 halves, no underrun.
- 256KB + a 75KB module: **refused outright before this round**; now loads and
  plays on the small ring.
- No sound card: module loads, falls back to viewer.

---

## 2. The rules this round produced

**The playback half cannot be halved and the record half can.** Both pacing
tasks sleep one tick and are correct only while a block outlasts that sleep.
A block is `half / rate`; a tick is 54.9 ms. Playback at 22,050 Hz is 1.69
ticks and halving puts it at 0.85 — under the pacer, with *nothing refusing*:
the rate check passes, the card starts, and the stream underruns for as long
as it plays. Capture had 4.97 ticks and keeps 2.49. (SPEC.md §34.6.1.)

**`mem_avail` is what a claim can GET, not what is unclaimed.** It counted a
purgeable block as occupied while `mem_claim` sheds to satisfy a request —
21.5KB reported on a heap out of which 18KB + 16KB was fundable. Every
consumer sizes itself *down* from that number, so under-reporting is a feature
silently lost, not a safe error. (§50.6.3.)

**Ask the allocator; do not subtract from an estimate.** A Tracker reserve
built as `avail - needk` refused a module the old code plays perfectly. Only
`mem_claim` knows what it would do. (§45.18.1, and §50.3 says the same thing
about `mem_claim_dma`.)

**A cache has a priority.** The owner's high byte is the rank, `0xFB`
trivial … `0xFE` high, with ordinary claims at `0xFF` — so the takeable test
is two compares and nothing takes a non-cache by construction. The shed takes
the *cheapest* block it outranks. (§50.6.4.)

---

## 3. The one thing designed and NOT built — BUILT, and the gate was broken

**One raise cache per window instance, instead of one for the machine.**
SPEC.md §11.96.3 is the section; it landed as designed below, with one
simplification (a `mem_pg_own` row needs a tag, a base and a COUNT — no
stride column, because §50.6 already requires every purgeable block to be
named by exactly one *word*). `.text` +69 bytes, no rung crossed.

**Read §5 first, though, because the gate this section told you to use did
not work.** `tools/sucheck.py` clicked a hard-coded point that Solitaire's
own rect contains, so it never covered anything, never entered `wm_su_take`,
and compared the screen with itself — reporting a *better* pixel figure than
a healthy run. That is also the answer to §4's "the claim is rare": it was
never rare, it was never happening. The gate is fixed, computes its click
point from the window rects, asserts the claim, and has a two-window phase
that is the discriminating test for this feature — with the control run:
old kernel 1 trivial claim, new kernel 2. Both restore at **0 differing
pixels**, on CGA, Hercules and VGA.

The design as it was written down, and as built:

Wanted because the single slot means last-covered-wins: covering a cheap
window takes the cache from an expensive one, and it is why SPEC.md §11.96.1's
third question ("is the repaint worth caching") has to be asked at all. With
the trivial rank in place the memory is genuinely free — anything that needs
it sheds it — so the bound stops earning its keep.

The design, worked out but not typed:

- `wm_su_seg`/`wm_su_win`/`wm_su_x1..y2` (6 words) become
  **`wm_su_segs: times MAX_WIN dw 0`** — one word per window, and `wm_su_win`
  disappears because the index *is* the window. `wm_ptr2idx` already exists.
- **The rect moves into a 4-word header at the start of the claim itself**,
  with `gfx_save` writing from offset 8. That is what keeps the cost to ~12
  bytes net instead of 120 — and 120 would cross the image rung (94 bytes of
  slack left) and spend a 512-byte step of `KERN_BUDGET`. It is also the
  better shape: the rect travels with the pixels it describes and cannot get
  out of step with them.
- The tag carries the window slot in its low byte (`MEM_P_WSAVE + idx`), so
  `mem_pg_own` needs a **stride and a count** per row rather than one word —
  `d = owner - tag; if d < count: word = base + d*stride`. That is the only
  part of the memory manager that has to change.
- Sites: `wm_su_take`/`try`/`ck`/`drop`/`rect`/`kb`, plus the four
  `cmp bx, [wm_su_win]` call sites (wm.inc 863, 1227, 2602, 3598), plus
  `wm_su_edge`, which computes its row count and stride from the rect and will
  need the header offset.

**Gate it with `tools/sucheck.py`** (added this round). Do not attempt this
without a working test — until Minesweeper/Piano/Solitaire opted in, *nothing
being driven made the promise*, so every scripted session saw an empty claim
map and there was no test of §11.96 at all. That is exactly the condition in
which a refactor whose failure mode is smeared pixels ships a subtle bug.
**And "a gate exists" is not "a gate works":** this one was written under
that warning and still tested nothing, for a reason the warning does not
cover. See §5.

---

## 4. Open, and worth someone's time

- **ModPlug pre-rolls TWO halves** — what Tracker had before the field report
  that raised it to six (§45.17.2). SPEC.md §56.1's predicted divergence
  between two independent replayer copies, arriving exactly as predicted.
  Unmeasured; wants a run on the iron.
- **Tracker cannot run on a 128KB machine at all** — its region alone is 48KB
  against a 36.5KB heap, so it fails with *Out of memory* before a window
  exists. The smallest machine that runs it is 256KB. XT mode is a CPU-tier
  property and every 8088 arms it, so "XT mode is every 128KB machine" is true
  of the *rate* and empty of Tracker.
- ~~**`wm_su`'s claim is rare.**~~ **Answered: it is not, and never was.** The
  flow driving it was clicking a point inside the very window it meant to
  cover, so `wm_su_take` was entered once in a whole session — at stage 1,
  on a window that makes no promise. With the click corrected it is taken on
  every cover and spent on every raise. §5, and docs/TESTING.md under "Prefer
  a self-checking harness to a careful one".
- **`tools/os88flush.py` was not used.** Reading the driver's counters over
  the debug connection turned out finer-grained than the guest-side log, but
  the flush harness is the right tool for anything about bytes on a disk.

---

## 5. Harness notes that cost real time

- ~~**`settle()`'s boot gate fires on the SPLASH**~~ **— fixed, and the
  workarounds are deleted.** `boot=True` is the wait again. The symptom as
  reported could not be reproduced on any machine buildable here (the 128KB
  one wants the IBM ROM this tree cannot ship, and on CGA, Hercules and VGA
  from reset the old gate went True at exactly the desktop and never during
  POST or splash). What WAS demonstrable is the mechanism that can produce
  it: the gate and the stillness test read the screen a round trip apart, and
  the emulator runs the guest several times faster than real time, so the two
  can answer about a state that never existed — a probe built that way
  reported the menu bar up while the same screen was 26% lit. One read serves
  both now, and the gate tests the whole desktop (bar field, its rule, the
  dock strip) rather than one band that another screen can imitate.
  docs/MARTYPC-DEBUG.md carries the measurements.
- **`m.sym()` returns a LINEAR address**, not an offset in `KERNEL_SEG`. Use
  `m.read(sym, n)`; `m.readseg(0x0060, sym, n)` reads code and looks plausible.
- **A driver's variable offsets MOVE whenever the driver's size does.** Derive
  them from a fresh `nasm -l` at run time rather than pinning them — a stale
  offset reads a plausible zero and cost one silent run of 0 samples.
- **New machines: `os8088_5150_sb_128k` and `_sb_256k`.** 128KB is
  `MIN_RAM_KB`, 36.5KB of heap — the only machine on which either sound claim
  can be judged. 256KB is where the sound apps actually live.
- **`heapmap.py`-style claim-map dumps are the instrument** for anything about
  memory: read `mem_tab` through `m.sym`, walk it, and print the free runs
  both ways (strict, and purgeable-as-free). Watching a refusal from outside
  the guest is how both kernel bugs were separated.
