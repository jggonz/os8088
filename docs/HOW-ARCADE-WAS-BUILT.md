# How ARCADE was built

A diary of `apps/arcade/`, kept as the work happened, for later reuse. It
records what was decided, what was measured and what was learned the hard
way. It is NOT how ARCADE works today: docs/ARCADE-SPEC.md is the
authority on that, and docs/plans/ARCADE-EMU-PLAN.md has the stories.

## The question

The emulator survey (docs/plans/EMU-SURVEY-286.md) put Space Invaders and
Pac-Man in the tier a 16 MHz 286 should run at full speed. The owner asked
for an arcade emulator with cores for several games, aimed at the 286 but
"as fast as possible" on an 8088, and said interpreted, recompiled or
ahead-of-time were all fair.

## Day one (2026-11-03)

**Space Invaders first.** Its CPU is an 8080, whose PSW is bit for bit what
the 8086's LAHF loads. Its screen is a one-bit bitmap. And its ROM is public
on archive.org (`arcade_invaders`), where every public item for Pac-Man is
not. (The owner later supplied a Pac-Man set for the Z80 wave.)

**A model before a design.** `tools/arcsim.py` began as a 200-line Python
8080 with the board's shift register and two interrupts. Run on the real
ROM with a scripted coin, start and play, it gave numbers no estimate would
have:

- 4,365 instructions a frame in play, 30% of them in the interrupt
  handlers.
- **One loop - counting the live invaders - is a third of everything**, and
  the main loop runs it 3.85 times a frame when once would do.
- Only 20 VRAM bytes a frame actually change.

**Ahead of time, not interpreted.** An 8KB ROM that never banks can be
translated whole when the package is built. Every 8080 instruction becomes
a label and a few 8086 instructions, and a 16K-word table maps every PC
for RET, PCHL and interrupts. The 8080's flags simply stay in the 8086's
FLAGS, and a liveness pass decides where a sequence of ours must save them.
The output is NASM text that the translator assembles itself, re-emitting
any short branch NASM finds out of range.

**The proof is a lockstep.** `tests/arclock.py` runs the generated code
under Unicorn beside the Python model and compares registers at every
instruction. Building it surfaced four honest differences, each of which is
now a stated rule:
- NOPs share an offset with the next instruction.
- The translation's own CALLs scribble below the stack.
- AC differs after a logical op (only DAA reads it).
- Flags that are dead at an interrupted instruction are garbage, legally.

It finished at 2.29 million instructions over 600 frames, all agreeing.

**The clock is the PC's.** The two interrupts are delivered on the PIT, not
by counting 8080 cycles. Space Invaders keeps its game time in its
interrupt handlers, so a slow host keeps game speed and only loses
main-loop passes.

**The first boot on MartyPC's 8088 ran the game at a crawl**, and the bugs
were all in the host:
- **The run length was measured in 16 bits.** Runs past 55 ms wrapped to
  small numbers, and the budget doubled forever.
- **Every interrupt handler's EI ended the run early.** The budget read
  those runs as "too short" and doubled again, until runs took 85 ms.
- **Budgets counted poll points, not work.** A spin-wait polls every few
  instructions and the main loop every few hundred. Weighting each poll
  point by its loop's size fixed that.

**Then a profile, not a guess.** Sampling CS:IP on MartyPC during play
showed the *host* taking 40% of the 8088. Fixed in turn, each measured
again:
- The per-write list became a per-CELL list, deduplicated by a flag at the
  cell's own key, and a store that changes nothing no longer lists anything.
- `IN 3` went inline.
- Key polls went from nine a frame to four.
- The CGA put was unrolled.

The 5150 went from about 2 frames a second to **59.62, the board's 59.54**,
with 1.2-1.5 main-loop passes a frame.

**A miss that looked like a hang.** Once, mid-game, the 8088 seemed to
stop. In fact the game had jumped through PCHL to an object handler (050E)
that the model's script had never reached. The package stopped correctly,
and the test script's own Space key kept relaunching it. That handler sits
in a ROM table the game copies to RAM, so the translator now walks every
word of the ROM's data tentatively, keeping only walks that decode cleanly.
Most of what that adds is zero bytes, which translate to nothing: 444 bytes
of code for 2,189 instructions.

**The 286 has room to spare.** On 86Box's 16 MHz 286 with VGA the main
loop runs about twelve times as often as on the real board, before any 286
tuning at all.

## Pac-Man: the second board, and the Z80

**The owner played Invaders on 86Box at full speed** and asked for the
next wave. Pac-Man's ROM came from the owner, not from an archive: a MAME
`pacman.zip` uploaded to the session, checked against MAME's hashes by
`tools/getarcrom.py pacman --from`, and never committed.

**The Z80 is the 8080 plus five problems**, each of which found us in
turn:
- **The code no longer fits a segment.** 16K PCs of jump table plus 57KB
  of translation is over 64KB, so the table moved to the AUX segment and
  every dispatch became `jmp word [es:si]`.
- **The 8086 has no parity-or-overflow flag.** P/V is the 8086's PF after
  logic and its OF after arithmetic; where a reader of P/V follows an
  arithmetic op, the translation copies OF into PF. N is not kept at all:
  every DAA in Pac-Man is reached only from additions, which the
  translator proves rather than assumes.
- **Helpers may not use the guest's stack.** Pac-Man reads the word below
  its own SP (`DEC SP` tricks), so a CALL into a block-move helper
  clobbered live data. Helpers now return through a cell (`ST_RET`) by a
  jump; nothing pushes below the guest's SP.
- **RST is a call with data after it.** Pac-Man's RST 28h is followed by
  three bytes of arguments, and the return lands past them. The model
  (`tools/arcpac.py seeds`) learns each RST's return distance by watching
  it come back; a first guess of +5 for RST 20h, seen once, was wrong, so
  a distance now needs RET or JP (HL) at the same SP in 90% of the runs.
- **(IX+d) can wrap past 32K.** Pac-Man's IX tables sit where IX+d sets
  bit 15, which on the board is a mirror. The translation masks A15 only
  where the decoder cannot prove it clear.

**The lockstep paid for itself again.** Unicorn runs the translation
beside a Python Z80 with the Namco board, comparing every instruction:
4.2 million Z80 instructions over 600 frames, through the boot tests, a
coin, a start and play. The harness itself had a bug worth recording: a
PC divergence was once skipped silently; it fails now.

**The picture was the work.** Pac-Man is 224x288 portrait; on VGA mode 12h
it is drawn 448x432, centred. The first composer decided a pixel at a
time and ran at a few frames a second on the 286. In order:
- **Planes, not pixels.** A host-side table (`arcpmgfx.py`) gives each
  tile row a mask per colour value; with a palette's four selectors, a
  plane byte is four ANDs and three ORs.
- **Sprites are boxes**, old and new, at most 4x4 tiles, composed with the
  tiles under them, so nothing is erased and redrawn.
- **A shadow of the screen**: a byte goes to VRAM only if it differs.
- **A per-tile plane cache**, invalidated by the board's tile writes, so
  a sprite box recomposes mostly from cache.

Three bugs there looked like the emulation and were the renderer: a
helper that clobbered BX (the row) overran the stack, a selector copy that
moved DI, and an 8-bit MUL for the shadow row that wrapped past row 255
and smeared the lower maze.

**Result**: full speed on 86Box's 16 MHz 286 with VGA, no frames dropped
in play (86Box is not cycle accurate; the proportion is the claim). The
5150 is not a Pac-Man target. Invaders kept its 59.4 frames a second on
MartyPC's 5150 through the multi-board refactor.

## Sound: a Sound Blaster, an AdLib, and nobody's recordings

**The ask** was Sound Blaster support "and/or AdLib, if it makes more
sense". It made sense to do both, because the two games ask different
things of a card. Pac-Man's sound chip, Namco's WSG, is three voices
reading 32-sample waveforms out of a PROM: that is PCM already, so on a
Sound Blaster it can be mixed exactly. Space Invaders' sound board is
analog circuits, so there is nothing to emulate - only something to
imitate. And an AdLib is the only card many period machines have.

**Space Invaders' sounds were synthesised, not sampled.** Emulators play
recordings of the real board; we ship none. `apps/arcade/arcsfx.py`
builds ten clips from a sentence each about what the circuit does - a
noise burst through a falling filter for the shot, a triangle swept by a
slower triangle for the saucer, a soft-edged square for each fleet step -
with a fixed noise seed, so the part is the same bytes on every build.

**One stream, fed to a lead.** The package keeps a 4KB ring the card plays
in place, and once a frame tops it up to 768 bytes past what the card has
consumed. Filling to a lead rather than "a frame's worth" follows the
card's crystal, so the two clocks never drift.

**Three things the driver taught us.** The open must stage a full 2KB
half, whatever block size the ring asks for. Asking the driver for the
stream's state every frame was a measurable share of an 8088, so it is
asked only when the consumed count stands still. And `op_fetch` - the
lazy read of the clips - may show a toast, which a full-screen bracket in
a foreign video mode may not, so it happens on the desktop first.

**The 8088 needed two passes.** The first mixer laid silence and then
added each clip into the ring byte by byte; the 5150 dropped 46 half
frames in 30 seconds. A CS:IP profile showed sound at about 4% of the
machine, with the game already near its edge. Now an 8086-class machine
mixes at 4,000 Hz (every other sample of the clips), every clip is summed
in registers in one pass and stored once, and at most three play at once.
The result: 0 or 1 half frames dropped in 20 seconds of play with fire
held.

**An hour lost to the instrument.** MartyPC's audio capture came back
empty - zero Sound Blaster samples, a full-length but silent AdLib file -
while the guest plainly played. The capture is drained only while the
emulator free-runs, and our scripts stepped it with `advance`. Reading the
mixed bytes back out of the guest's own ring sidestepped it for the
stream; for FM the row now plays under `run` and waits on guest time.
docs/MARTYPC-DEBUG.md carries the warning now.

**Pac-Man on the 286.** The mixer is a four-instruction inner loop per
voice per sample, four samples a pass. 86Box shows the chase and death
scenes dropping a few frames a second with a card in the machine whichever
route plays - the cost of the sound driver being loaded at all, since the
same scenes drop none on the same machine without a card.
