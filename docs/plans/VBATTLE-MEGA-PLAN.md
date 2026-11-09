# VBATTLE-MEGA — Video Battle on kern_mega, in a real-mode bracket

**Status: W1 to W5 BUILT (2026-11-06, SPEC.md 126.4.5).** The three queued items below are next. SPEC.md 126.7 said "kern_mega is not
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

- [x] XMS: the hold opened at the door (`vb_load`, once: the block, the
      EMS pages and the bounce claim are refused inside a real-mode
      bracket), filled inside through `OSAPI_XMEM_COPY`'s twin; the bounce
      pinned for the bracket and swapped with the rest. The read in place
      needed NO new address: on kern_mega a token is the block's linear
      address while the pool leaves the block where it is - the Video
      Player's door tests the same thing (its `MOVED` reason) - and the
      battle's in-place check already compares 16 bytes read through the
      twin with the same 16 read in place, so a moved block fails it and the
      hold copies. It is run again at each bracket's start (a 286's LOADALL
      state is that bracket's too). `vbtmega --arm hold` on Bochs's 386:
      568 KB held, read in place by the loaded base, no disk read from the
      fill on, the play again's title exact, a second bracket in place again.
      A MOVED block is not provoked by the row.
- [x] EMS: `FSXL_DRV DRVC_EMS` and the frame in the swap list (W3), run in
      W4 on 86Box.

  **What W3 found.** `vb_mklist` ended on an `stosb` that relied on CF = 0
  from an `or`; W3's new `cmp byte [vb_htype], 2` before it left CF = 1 with
  an XMS hold, which read as "a claim above 1MB" - every held play refused.

### W4 - the other machines

- [x] 86Box's mr286 through kern_mega's port (`vbtmega --emu 86box`, row
      `vbtmega86`, 107 s): protected mode left by the CMOS shutdown RESET;
      the play to victory and the play again exact on CGA and on HERCULES;
      a Sound Blaster 2.0 fed on the emulated clock (15 s consumed, never
      halted); the XMS hold read in place by a 286's LOADALL (131 records,
      157 yields); an EMS hold - a Lo-tech board, which the port learned to
      add (`_ems`, `tools/os88box86api.py`: `vm/286-mega-ems` has no CMOS
      seed and stops in setup under the port) - read in place through the
      frame. No disk read from either fill on. Red with the door blind to
      `CPU_F_PM`: the package CLOSED at the first Play. **No engine change**:
      every leg ran W1-W3's code; the one miss was the row's own Hercules
      decode, which forgot the picture sits at byte 5.
- [ ] Not run: VGA under kern_mega on 86Box (Bochs's VGA is W1's), the
      owner's own Victory or 5150 hardware.

### W5 - the region and the title picture

- [x] `OS88_HIGH | OS88_HIGHMEM` declared, `tests/megahigh.txt`'s two lines
      retired. Every claim the bracket reads or sums takes the DMA door with
      no head (`OSAPI_MEM_CLAIM_DMA[_HI]`, CX = 0: conventional on every
      kernel, Video Player's way) - the resident claim, the rings and their
      packing probe, the hold's bounce; the card's ring always did. On
      kern_mega the region is then above 1MB on the desktop, and the door
      leaves its room too (`vb_rkeep` + `VB_REGKB`, the image and bss in KB,
      when `SEG_REAL` has no paragraph for DS): the bracket copies it down.
- [x] The title picture is the one claim free to go above 1MB, and there it
      costs a play nothing - so `vb_cvplay` keeps it through the play when
      `SEG_REAL` says it is high, and the window is the picture the moment
      the play returns (on kern_big, and in conventional memory, it is
      given back as before). `vbtmega --arm play` on Bochs: the region at
      FD4C00h, the picture at FD0C00h, kept through the play, the window the
      picture as it returns; red with the picture freed at the door as
      before (kept and the window both FAIL).

## Queued (the owner, 2026-11-06 - after W1, before or between the waves)

- [x] **Victory's line** (2026-11-06, SPEC.md 126.3.1). The engine chose
      among six built-in lines by the ending's kind, and an end clip's said
      "Enter: again" while the victory clip still played and Enter did
      nothing. The builder now writes each ending's line into the bundle
      (format 9, header 356): the author's (the Title & endings tab's "Its
      line") or a default naming only what works from the moment the line
      appears - "VICTORY!   Esc to leave" for a hold with no end clip,
      "VICTORY!" otherwise. The engine is 196 bytes smaller; `vbtloops`
      holds both kinds.
- [x] **VGA tears** (2026-11-06, SPEC.md 126.3.2). `IKARU.V88` turned out
      to be PAGE FLIPPED (its title says "Page Flipped"; two pages): each
      frame is drawn on the hidden page, and its few flashes at the tunnel
      are its scene palette changes (frames 830, 845, 1046). The battle's
      VGA4 wrote mask by mask over the whole visible frame, so a changing
      pixel was a colour of neither frame for the whole decode. The builder
      now writes the partial masks in bands of rows (`target.vga_bands`, 16
      by default; the Battle tab's "VGA writes": whole frame, 32, 16, 8),
      the engine unchanged. On 86Box's VGA XT with the owner's footage
      (debug only): 308 flash pixels a sampled frame whole, 71 / 30 / 22 at
      32 / 16 / 8 rows, for +6% / +12% / +24% record bytes. `vbtvgaflash`
      is the row. Page flipping - none at all, for a two-frame delta and
      the band drawn on both pages, and engine work - is the option left
      if bands are not enough.
- [x] **VGA's colours** (2026-11-06, SPEC.md 126.3.2, bundle format 9).
      Two causes: four of eight colours were the band's CGA ones (cyan,
      magenta: the "red, teal"), and the rest a pixel-count k-means that
      puts colours where most pixels are (the greys). Now all K are chosen
      from the footage by Video Encoder's `colour_kmeans` (`--palette
      colourN`: CIELAB, chroma-weighted, black fixed); the band takes the
      nearest four DIFFERENT entries through a band map the engine applies
      on all four planes; keeping the band's CGA colours is an option (the
      Battle tab). Each colour's pixel value is Video Player's
      `PLANE_CODES` in Gray order, so a change to a neighbouring brightness
      is one store: with the 16-row bands, 25 flash pixels a sampled frame
      where the plain codes left 135, and 8% fewer bytes than the old
      palette's. Format 9 also moves the sixteen colours to 464 by pixel
      value: at 352 their sixteenth overlapped the hold order's count, and
      a 16-colour rendition's hold was garbage (the hold order now holds 16
      ranges). No legacy reader: nothing has shipped (the owner).
- [x] **VGA colours, step 1: the dither** (2026-11-06, SPEC.md 126.3.2).
      The owner, after the palette fix: "blue things choose to be a dither
      of red and yellow ... red things choose to be brown". The palette had
      the colours; the dither did not use them - it offset all three
      channels by one grey amount and took the nearest entry, so it
      dithered brightness. `quant_vga` is now Video Player's
      `KnollDitherer` (a pattern whose mean is the source colour): a pink
      74 -> 11 mean error (`vbtgui`), the records +1.5% on scratch footage.
- [x] **Step 2: the colours by error** (2026-11-06, SPEC.md 126.3.2):
      `vbtana.plan_colours` - each stream's own first, then the shared,
      each the candidate that most lowers the battle's weighted CIELAB
      error, then swaps and Lloyd steps; black and white always shared,
      the idle loop weighted three times, the band's colours a stream of
      their own. Shared-first gave one clip's colour to every clip
      (`vbtgui` holds the order).
- [x] **Step 3: each clip's own colours** (format 10): a VGA4 record may
      load the DAC after its picture (`80h | N` and N entries;
      `vb_decode`); a clip's first frame and its last before a seam onto
      the idle loop use the shared alone, the records that draw its frame
      0 load its own and the one before the seam the loop's, so no load
      ever changes a pixel on the glass - `verify` refuses a bundle where
      one would. Measured: at 16 colours the mean block error 6.1 / 3.2
      (Mandelbrot title / Ikaruga clip) against one palette's 6.8 / 6.9;
      at 8, 11.6 / 6.2 against 11.0 / 9.4.
- [x] **The joins, fixed** (2026-11-06, SPEC.md 126.3.2). Step 3 drew a
      clip's first frame in the shared colours alone so a load after it
      changed no pixel; that frame popped at every join (70% of the canvas
      moved > 10 dE into the next frame, against ~34% for any frame). Now
      a load goes FIRST in the record that draws frame 0 - a cut, Video
      Player's rule for scene palettes - and the idle loop uses the shared
      alone, so an action's joins load nothing over its pixels and its
      seam back loads nothing. `verify` models the DAC on every path.
- [x] **The tearing back**: the dither now HOLDS (Video Encoder's
      `--vga4-hold` 24, score 60): a noisy still picture's changed pixels
      a frame 21.9% -> 1.1%, a moving clip's error 2.3 -> 2.5.
- [x] **The owner's next round** (2026-11-06): per-clip colours OFF by
      default (`target.vga_clip_colours`; a clip's own browns dithered its
      background another way than the idle loop's, and turning footage
      moves everywhere) - every colour shared, chosen over the loop and
      every clip; an action's dither STARTS from the idle loop's screen
      (the join 8.5% -> 0.0% of the canvas on test footage); the colours'
      VALUES chosen against the tearing (half-written pixels > 20 dE off
      35.9% -> 17.4% of changes); VGA's red a grey-out, so the band keeps
      only ground and white; and VBATTLED.O88, the profiling engine
      (SPEC.md 126.8, `tools/vbtprof.py`, the `vbtdiag` row).
- [x] **Step 4: 8 and 4 colours** - 1 of 8 a clip's own and 4 of 16
      (`target.vga_clip_colours`, the Battle tab, "auto" the default); at 4
      none, black and three from the footage, white not kept; "Keep the
      band's CGA colours" at 4 is CGA's own four.
- [x] **Step 5: band pictures in VGA's colours** (SPEC.md 126.5). The
      owner's menu is a band PICTURE - a blue window - and the band is
      CGA's two bits a pixel on every adapter, so on VGA the window was
      dithered into the band's four CGA colours (red and yellow). Now each
      picture has a VGA version in the shared colours, kept in a layer
      after the ground's rows; the present shows the layer wherever the
      band's buffer is its ground, so text and the cursor stay the band's.
      The pictures' pixels count in the shared colours' choice.
      `vbtadapters` holds it on MartyPC's VGA (the window's mean DAC 8, 13,
      35: blue); red with the layer off. +415 bytes of code, 88 of bss.
- [x] **A palette viewer on the Build page** (2026-11-06, SPEC.md
      126.3.2): the battle's K colours, black the base and the band's four
      marked "menu", beside the memory lines; a clip picked in the budget
      shows each colour's share of its pixels. Until step 3 a clip has no
      colours of its own, so its view is its use of the shared ones.

## What it costs

Measured per wave, against the byte, on the shipped engine:

| wave | image | bss | packed file |
|---|---|---|---|
| W1 | 24,337 -> 24,887 (+550) | +69 | +451 |
| W2 | no engine change | | |
| W3 | 24,887 -> 24,963 (+76) | +1 | +49 |
| W4 | no engine change | | |
| W5 | 24,963 -> 25,011 (+48) | +1 | +34 |
| VGA colours step 3 | `vb_decode` 101 -> 127 bytes (+26: the palette load) | 0 | |

W1's split (claims at the door, `vb_lread`, `vb_derive`) is shared code;
the kern_mega markers are registered in `tests/megapkg.txt`.
