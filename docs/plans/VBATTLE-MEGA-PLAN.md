# VBATTLE-MEGA — Video Battle on kern_mega, in a real-mode bracket

**Status: W1 and W2 BUILT (2026-11-06, SPEC.md 125.4.5).** SPEC.md 125.7 said "kern_mega is not
supported", and that was true in a worse way than it sounded: the SHIPPED
engine, opened from an ordinary disk on a kern_mega machine, was CLOSED by a
#GP the moment Play was pressed (measured on Bochs, below). This plan makes
the one shipped engine play there.

## The owner's decisions (2026-11-06)

- **One engine.** The kern_mega support goes in the SHIPPED VBATTLE.O88, not
  a `PKG_MEGA` arm: every battle disk, the builder's zip and every game made
  with "Make the games" embeds that engine, and a second engine would make
  each of them refuse on kern_mega. The kern_mega-only part is small (the
  `FSXF_REAL` list, `apps/os88real.inc`, one test of `CPU_F_PM` at the door)
  and is registered in `tests/megapkg.txt`; the rest is a restructuring the
  real-mode kernels share.
- **Real mode or a refusal.** The play runs in `FSXF_REAL`'s real-mode
  bracket (SPEC.md 53.2.3, the Video Player's 98.3.24). When the kernel
  falls back to protected mode (`FSXR_*`: the bundle on a CD-ROM, a network
  share or a RAM disk, a file in more than 224 pieces, no conventional room
  for a driver's low copy), the play is refused and the window says why.
  There is no protected-mode play path.

The owner's pointer: *"video player runs there, so we should be able to copy
a lot of its mega techniques (including, critically for video play, 'run as
real mode in fullscreen')"*.

## What was found first

The shipped engine on kern_mega (Bochs, VGA, the demo battle on B:):
`kern_mega CLOSED Video Ba 0D at 10D4:0C5E` at the first Play - `vb_load`'s
`mov es, [vb_sfxseg]`, a segment made by adding paragraphs to the resident
claim, which on kern_mega is an LDT selector. Every part of the play that
derives a segment does the same: the resident claim's six parts, the rings'
records (`RG_SEG` plus a cursor), the entries (`vb_entcur`).

## The shape

Everything that CLAIMS happens at the door, on the desktop, in protected
mode; everything that ADDS PARAGRAPHS happens inside the bracket, in real
mode. That is Video Player's split (98.3.24's door, `os88real_prep`,
`os88real_swap`), and on Video Battle it is a split the engine nearly had:

| at the door (`vb_play`, both kernels) | inside the bracket (`vb_main`) |
|---|---|
| the header and the sizing, the resident claim (once, kept between plays) | kern_mega: the claims' words swapped for real segments, the six parts derived, the block, the effects, the idle loop and the band's pictures read (once a session) |
| the card's ring, the two clip rings (`vb_rclaim`, moved out of `vb_main`: a real-mode bracket refuses a claim) | the music's ring filled, the play, the play again |
| kern_mega: `os88real_prep`, the `FSXL` list (the bundle's file, SOUND.DRV), `FSXF_REAL` | kern_mega: the words swapped back before the entry returns |
| after: the rings and the card's ring given back (`vb_between`) | |

kern_big and kern_small keep the load on the desktop, where the window says
"Loading..." over it; on kern_mega the load is the bracket's first second
(a black screen, with the play's mode already set).

## Stories

### W1 - the demo plays on kern_mega (Bochs, VGA, no card)

- [x] The rings claimed at the door on every kernel; `vb_load` split into
      the claims (`vb_load`) and the reads (`vb_lread`), the six parts
      derived by one routine (`vb_derive`).
- [x] Under `CPU_F_PM`: `os88real_prep` over the claims' words, the list,
      `FSXF_REAL`; inside, the swap, `vb_derive` and `vb_lread`; the swap
      back on every way out.
- [x] A bracket the kernel ran in protected mode does nothing and the window
      says "Needs real mode:" and the reason.
- [x] The hold (XMS, EMS) is not opened under `CPU_F_PM` (W3).
- [x] Verified: a new row on Bochs plays the demo to victory with
      `[fsx_rmode]` seen 1, no FAULT or CLOSED; the MartyPC `vbt*` rows
      unchanged; kern_big's engine bytes measured.

  **What W1 found.** Each ring's END (`RG_END`, where its reads wrap) and
  its places were summed from the SELECTOR at the door, when `vb_rclaim`
  moved out of the bracket; only `RG_SEG` was in the swap list. The first
  title fitted before a wrap and was exact in B8000; the play again wrapped
  and left 192 bytes of garbage. They are summed again inside now.
  `vb_nrsay` copied its prefix through AL, which held the reason: every
  refusal said "?" until the row's refuse arm caught it.

### W2 - sound through a card

- [x] `FSXL_DRV DRVC_SOUND` when the play has a card; the card's ring
      conventional (`OSAPI_MEM_CLAIM_DMA_HI`) and in the swap list - both
      already W1's, and W2 needed no engine change.
- [x] Verified on Bochs's SB16 (`vbtmega --arm card`): the card's ring
      claimed, the music never dry, the card never found halted, 12.5 s of
      the stream consumed in the bracket, and Bochs's capture IS the
      battle's music - its best correlation with the bundle's music 0.67 at
      the bundle's own rate, 0.41 at most at any other resampling (Bochs's
      own resampler softens the demo's square-ish synth, so a clean copy
      scores ~0.6-0.7 there, which is why the test is the margin). Red with
      SOUND.DRV left out of the list: a silent play, nothing consumed.
      **Not measured here**: an action's own sound against its picture
      (`vbtownsnd`'s frames) - Bochs's PIT is not on an 8088's clock; that
      is W4's, on 86Box.

### W3 - the hold

- [ ] XMS: the block allocated at the door, filled inside through
      `OSAPI_XMEM_COPY`'s twin; the read in place (LOADALL, a 386's base)
      inside a real-mode bracket, the token's address as Video Player gets
      it (98.3.18.11).
- [ ] EMS: `FSXL_DRV DRVC_EMS`.

### W4 - the other machines

- [ ] 86Box's 286 (the reset path, docs/86BOX.md), CGA and Hercules under
      kern_mega, a Sound Blaster on the emulated clock.

### W5 - the region and the title picture

- [ ] `OS88_HIGH` (the region copied down for the bracket) and
      `OS88_HIGHMEM` where a claim can be above 1MB - retiring
      `tests/megahigh.txt`'s two lines.
- [ ] The title picture read back without a claim inside the bracket (a
      real-mode bracket refuses one), so the window does not show its text
      layout first.

## Queued (the owner, 2026-11-06 - after W1, before or between the waves)

- [ ] **Victory's line.** "Victory! Enter: again / Esc: exit" is shown even
      with an end video attached, where Enter does nothing. Make the line
      editable in the builder, and the default, where the end does not hold
      its last frame for a key, just "Victory!".
- [ ] **VGA tears.** Planar tearing: things shimmer and flicker where they
      change. Write in bands, or use the second page - both cost speed - or
      first look at what a clip that barely tears does differently: the
      owner's `IKARU.V88` (kept in the session's scratch, not the tree),
      which tears little and, as the owner recalls, uses neither.
- [ ] **VGA's colours.** The VGA rendition keeps to red, teal and greys. It
      should choose its palette the way Video Encoder's `colorX` does, and
      the CGA focus should stop being forced: one option among the rest.

## What it costs

Measured per wave, against the byte, on the shipped engine:

| wave | image | bss | packed file |
|---|---|---|---|
| W1 | 24,337 -> 24,887 (+550) | +69 | +451 |

W1's split (claims at the door, `vb_lread`, `vb_derive`) is shared code;
the kern_mega markers are registered in `tests/megapkg.txt`.
