# Kernel size pass 5: the record, and what is left for a sixth

**PASS 5 HAS LANDED** on `kernel-size-p5`, cut from `elendilon` at
`dfbe2b3b`. The file is named `-P6` for the reason pass 4's record is named
`-P5`: each pass's record is named for the pass it hands to. Its
companions, and this file repeats none of them:

* **`docs/plans/completed/HANDOFF-KERNEL-SIZE.md`** — pass 1's handoff, still
  the authority on **method**.
* **`docs/plans/completed/HANDOFF-KERNEL-SIZE-P5.md`** — pass 4's record. Its
  §2 and §3 are still the list of costed and refused items, and nothing here
  re-proposes a §3 refusal.
* **`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-09-25.md`** — the
  per-concept attribution of the cycle between the #197 and #199 squashes,
  which was this pass's starting list for that cycle.
* **`docs/KERNEL-MEMORY.md`** — where the budgets stand. Blessed at the close
  of this pass.

---

## 0. THE BRIEF AND THE OUTCOME

The brief: a size pass aimed at **work that had not had one** — mostly what
landed since the #199 squash, plus the concepts between the #197 and #199
squashes that pass 4 did not reach. Optimisations outside that work were
welcome. The hot-path graphics that had landed were in scope **on condition
of losing no more than a trivial amount of speed**; for everything else the
target was **50% of the resident bytes the work added**.

Resident = `.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf`, measured by
`tools/kernsize.py --json`, never by rungs:

| | base `dfbe2b3b` | close | Δ |
|---|---:|---:|---:|
| kern_big resident | 100,886 | **99,960** | **−926** |
| kern_small resident | 68,972 | **68,595** | **−377** |
| kern_big `KERN_SIZE` | 107,008 | 105,984 | −1,024 (the cold rung the new work had crossed, uncrossed) |
| kern_small `KERN_SIZE` | 71,168 | 71,168 | 0 |
| kern_big `.text`+`.bss` left of `KERN_CODE_MAX` | 12,376 | 12,667 | +291 |

**The pass bought back more than the cycle since pass 4 spent.** The base
stood +894 resident bytes over pass 4's blessed baseline on kern_big; the
close stands 32 under it.

## 1. WHAT THE WORK ADDED, AND WHAT CAME BACK

Every kernel-touching commit since the squash was measured against its own
first parent on both kernels (clean worktrees, each tree's own `kernsize`);
the earlier cycle's figures are the 2026-09-25 report's. The five topics
were worked in parallel by one agent each, in private worktrees, and merged
with `--no-ff`; every merge was re-measured, re-checked for cross-section
jumps (§3) and re-run on a combined soak set before it was pushed.

| topic | added (big / small) | from the topic (big / small) | outside it, same agent (big / small) | total (big / small) |
|---|---:|---:|---:|---:|
| **video** — `FSXF_RATE`, the progress-box fence, `OSAPI_FILE_READ_SEQ`, the resize lock, `FERR_BIG` | +530 / +73 | −194 / −38 (37% / 52%) | −17 / −13 (`fsx.inc`) | **−211 / −51** |
| **gfx** (HOT) — `gfx_blit1` / 1bpp `font_run` window clip, `gfx_blit1`'s fast path, `gfx_blitp` plane-major | +453 / +85 | −42 / −8, **and faster** | −4 / −7 | **−46 / −15** |
| **assoc** — a document's program, `ASSOC.DAT` in one track, stale hints, unknown extensions | +227 / +31 | −82 / −5 (36%) | −206 / 0 (the rest of `assoc.inc`, and version 1 dropped) | **−288 / −5** |
| **compress** — streamed Compress, the decoder's growth, the truncate | +200 / +94 | −45 / −23 (23% / 24%) | −63 / −63 (`diskw.inc`'s shared tails) | **−108 / −86** |
| **small** — mouse IRQ hand-back, save-under, warm motor, far tail, sound, `dsk_path_x`, drive icons | +281 / +166 | −35 / −17 (12% / 10%) | −238 / −203 (pass 4's `wm_clip_n` item, epilogue and shared-tail sweeps) | **−273 / −220** |
| **total** | **+1,691 / +449** | **−398 / −91** | **−528 / −286** | **−926 / −377** |

**Against the 50% target, honestly.** Only the associations topic cleared 50%
on its own bytes when the whole file is counted (−288 of +227); taken to its
own commits it is 36%. The others stopped short for measured reasons, below.
The pass as a whole took out 926 bytes against the 1,238 non-hot bytes the
work added (75%), and more than half of that came from outside the topics —
which the brief allowed, and which is where the remaining easy bytes were.

### 1.1 Why each topic stopped where it did

* **video (37%)**: ~100 bytes are the rate hook and its ISR arm inside IRQ0,
  and READ_SEQ's cursor copy, generation check and cell are its contract.
  All three hot paths got FASTER (`sch_isr` idle 331 → 326 cycles, rate
  entry 528 → 507, hook to callback 319 → 275); the streaming read is
  unchanged at 219.7 ms a 32KB call. **Offered, not taken**: READ_SEQ as an
  on-demand module (~−140), which would make a video on a data disk ask for
  the system disk, because `mod_need` loads only from the boot volume.
* **gfx (9%, hot)**: every topic byte is per blit or per row. Both kept
  changes are smaller AND faster (the covered-blit fragment walk −0.4% on
  Hercules, CGA and VGA; the fast path −0.16 to −0.22%). A version that
  shared `gfx_rect_isectcf` through a far entry was 19 bytes smaller and
  **+1% a covered frame**, and was rewritten inline (SPEC.md 5.4.2.7).
  `font_run_cell`'s column merge would cost +15 cycles a glyph row (~3% a
  cell); `gfx_blitp` plane-major had already been squeezed.
* **assoc (36% own)**: what remains is reached from the double-click path
  and `OSAPI_PKG_START`, neither of which loads a module.
* **compress (23%)**: the input-crossing decoder serves the transparent read
  of any compressed file of 64KB or more, not only Compress, and the truncate
  IS `OSAPI_FILE_WRITE_AT` with a count of 0 (the DOS box's `AH=40h CX=0`
  calls it). Decoder cycles unchanged or better (BEVERLY.MOD LZ4 6,454,875 →
  6,391,011).
* **small (12% own)**: the mouse re-arm's vector, MCR, IER and 8259 writes
  are the fix itself (~70 bytes); the icon art is 25 pool rows at one byte a
  run.

### 1.2 Behaviour changes, all deliberate

* **`ASSOC.DAT` version 1 is no longer read** (SPEC.md 54.3.2; the owner's
  decision, −38). A v1 volume is a cold cache: nothing refused or drawn
  wrong, one header read per package on listing, and the next install writes
  v2. Every shipped disk and both writers were already v2.
* **READ_SEQ over a broken FAT chain** fails the next call with `FERR_IO`
  directly instead of re-seeding by name and failing with the same error;
  cursor field +6 holds the last cluster read. `SCH_RATE` is 5, was FFh.
* **A truncate whose FAT flush fails** re-syncs the listing before
  `FERR_IO`, which is how a delete already behaved (SPEC.md 18.4.7.5).
* **The LZ decoder returns DI one past the last byte written**, as its
  contract says; before, it returned the tail length. No caller reads it.
* **The `Needs <STEM>.O88` toast** is read straight out of `assoc_fnb`.

## 2. WHAT IS LEFT, costed

| candidate | kern_big | kern_small | why not taken |
|---|---:|---:|---|
| `OSAPI_FILE_READ_SEQ` as an on-demand module | ~−140 | 0 | a design trade: a video on a data disk would need the system disk in (the owner's call) |
| READ_SEQ swaps the cursor instead of copying it | ~−11 | | the caller's cursor would briefly hold junk during the call |
| READ_SEQ drops the cursor's volume check | ~−15 (−10 net) | | `dsk_vol_del` and `clone.inc` write `[disk_drive]` without a generation bump |
| the covered-blit walk through `gfx_clip_run` in `.text` | ~−50 | | +22 bytes of stack on a covered blit and ~+90-100 cycles a fragment |
| kret-ladder exits in `vga12.inc`/`font.inc`/`softgfx.inc` | ~−62 | | per primitive, per glyph or IRQ4's save-under: a taken `jmp` each |
| six "preserve everything" assoc routines onto `kentc_di` | ~−18 | | ~60-80 cycles a call, per icon |
| `ct_cw_snd_beep` gated on kern_small | | −2 | `%ifdef` noise |
| dead `clc` after `xor`: `files.inc`, `snd.inc`, `wm.inc`, two in `diskw.inc` | −5 | | one byte each, found late |

## 3. A near jump across sections: NASM is silent, `ovlchk` is not

In `-f bin`, a near or short `jmp`/`call` to a label in ANOTHER section
assembles cleanly — the displacement is computed across the two sections'
`vstart`s — and at run time lands in the wrong bytes. The small-items agent
wrote `jmp kret_dx` in `clk_bios_write`, which a source scan read as `.text`
and which is `CTRL.DRV`'s `.modc` on both kernels, and caught it before commit
by checking every tail-jump target against NASM's `[map all]`.

**The build would have caught it too.** `tools/os88ovlchk.py` (the `ovlchk`
fast row, run by every `make`) walks every section block and refuses exactly
this; planted back into the tree, the same line reads
`kernel/clockw.inc:534: .modc -> .text, near: kret_dx` and the fast tier
fails. The pass also built an independent ASSEMBLY-based check (each branch
with emitted bytes, its section taken from the nearest preceding label in the
`[map all]` output, since `nasm -l` lists the lines of a false `%if` branch and
`nasm -E` refuses the kernel's symbol-valued `%if`s). It agreed with `ovlchk`
at the base and at the close — one deliberate crossing, `jmp boot2_entry` — and
it was NOT added as a row: it duplicates a gate that already works. **A shared-
tail sweep is exactly the change that writes this defect, so run `make` (or
`python3 tools/os88ovlchk.py`) after every batch, not only at the end.**

## 4. METHOD LESSONS

* **All five agent worktrees started at the last squash (`6212352d`), not the
  branch cut.** Every one noticed because the brief said to check `git log -1`
  and quote the base figures; all reset to `dfbe2b3b` and matched them to the
  byte. Put the base commit AND its figures in the brief.
* **Merges were clean textually and were still re-checked.** Three agents'
  shared-tail sweeps met in `wm.inc`, `diskw.inc` and `disk.inc`; each merge
  re-ran the section check and a combined soak set before its push.
* **One MartyPC build, shared by symlink**, served five worktrees: instances
  are private per launch, so `build/martypc` can be one tree.
* **A source parser in a gate is part of the code it gates**: `assocpage`
  failed on a `mov cl, N` after `rep` and was taught the form, accepting it
  only after a `rep`.

## 5. WHAT WAS RUN

The fast tier at every agent commit and every merge; `make small` at every
merge. Per topic, the rows each agent's change could reach (video 26 rows,
gfx 28, assoc 21 + 15, compress 22, small 25 + 16). On the merged branch, after
each merge: 19 rows (video, associations, file copy, heap, window drag, mouse
grab, `small128`, `bootsmoke`), then 13 (the decoder, the truncate, the DOS
file calls, `vidkern`), then 14 (`vidlive`, `vidlivevga`, `blitplane`, `blitp`,
`dispband`, `disptitle`, `paint1bpp`, `paint1small`, `telpen`, `dotdel`,
`dispdrag`, `wdmenusu`, `small128`, `bootsmoke`) — all green. **`dispsave`
fails at the base `dfbe2b3b` as well** ("no raise cache was taken for a window
on display 1") and is not this pass's; `ddcorner` failed once under two-lane
load and passed alone. The whole soak tier was not run: it is the owner's to
ask for.
