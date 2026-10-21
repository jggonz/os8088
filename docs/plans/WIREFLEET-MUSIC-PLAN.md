# WireFleet — missiles, impacts and music

The third WireFleet delivery. The binding contract is SPEC.md §114.11; the
diary continues in [HOW-WIREFLEET-WAS-BUILT.md](../HOW-WIREFLEET-WAS-BUILT.md).

The owner's brief, 2026-10-20:

1. Add a music track that silences itself while the missile launch, splash
   and explosion sounds play.
2. Animate a shot: the launch, the missile travelling to the landing square
   on the other side, and an explosion on a hit or a splash on a miss.
3. Codex image generation may make the art; sounds may come from the web,
   played on the Sound Blaster or the speaker.

## What the OS already had (docs/INDEX.md first)

- **Music**: Gorillas' FM sequencer (SPEC.md §99, `apps/gorillas/grbgm.inc`)
  claims three FM channels by patch-load and leaves channel 8 to the tone
  tier. WireFleet's is the same shape with its own track and a duck.
- **Samples**: 1942's effects (§101.6) are three external 4KB rings in one
  page-safe claim, filled from a lazy part and played with
  `SND_OPENF_RING + SND_OPENF_EXT`. WireFleet's are the same, to the byte
  layout.
- **Sprites over a cell**: WireFleet's own hit X is a stamp,
  `(pixel AND a) OR o`, proven exact by its converter (§114.10.2). Missiles,
  splashes and explosions are sixteen more stamps through the same compose
  loop, so all four faces (VGA/EGA packed and planar, 1bpp, the CGA bracket)
  get them from one mechanism and the cell cache stays the only redraw.
- **Room**: the image and bss were 60,416 of 64KB, so the stamps and clips
  are lazy parts (§20.12): the stamps at window open, the clips only on a
  Sound Blaster.

## The fork, and why

Hiding the result until the missile lands could have been a protocol change
(a "landed" message) or a presentation mask over state already applied. It
is neither: the receiving side simply **takes the frame later**. The
shooter stashes the first RESULT that arrives during its flight; the
defender starts its incoming flight on the first SHOT and judges on
landing. §114.6 already tolerates a late answer - the shooter repeats SHOT
and the defender replays its cached RESULT - so the wire, the history hash
and the save format are untouched, and the text lines, the sunk reveal and
the cues change at the landing because the state does.

Speaker PCM on the desktop (`os88pcm_play`, §34.4) freezes the machine for
the clip, which would stop the animation it accompanies, so the speaker and
AdLib routes play tone forms of the three effects instead.

## Delivery stories

- [x] **A shot is a missile, on both PCs.** Launch flash and smoke on one of
  the shooter's afloat ships; the missile crosses to the target in cell
  steps, pointing its way (six directions), smoke behind it; on the
  defender's PC it comes from a random enemy-water square. Verified: QEMU
  pair, screenshots of every frame on both PCs, a miss and a hit.
- [x] **Explosion or splash, then the mark.** Four frames each; the result,
  text and sunk reveal appear at the landing. Verified: the same run; turns
  and history hashes commit as before.
- [x] **Every face draws it.** VGA planar (QEMU), Hercules, windowed CGA and
  the CGA 320x200 bracket (MartyPC, all sixteen stamps over water and over
  a ship); 1bpp and 2bpp stamps carry a black halo so they read over a
  ship's stipple.
- [x] **A music track that ducks.** "Ironclad", 21 s, three FM voices, or
  the melody on the speaker below every cue; keyed off for an effect and
  resumed at its row nine ticks after. Verified: QEMU SB16+AdLib card
  capture (music, effect, silence, music) and the speaker capture.
- [x] **Sampled effects on a Sound Blaster, tones elsewhere.** CC0 clips
  (Kenney, Thimras), derived reproducibly and credited.
- [x] **U turns the music alone off and on**, Game > Music, in the window and
  the bracket; M still silences everything.
- [x] **Contract and host checks.** SPEC §114.11, `wirefleetart` covers the
  stamps, the song and the clips.
- [ ] **Measured on a 4.77 MHz XT** - a flight's cost per tick on MartyPC's
  VGA and Hercules XTs, taken in the final pass.

## Rows this touches

`wirefleetart`, `wirefleetdeploy`, `wirefleetlink`, `wirefleetsystem`,
`wfdraw`, `wfdrawherc`; the fast tier's `pkg` and `asmrules`.
