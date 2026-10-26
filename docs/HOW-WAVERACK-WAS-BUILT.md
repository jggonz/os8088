# How WaveRack was built

A diary kept while the work happened, for later reuse. Observations and
intentions are kept apart: what was MEASURED or SEEN says so, and what was
planned says that. The plan is [WAVERACK-PLAN.md](plans/WAVERACK-PLAN.md)
and the contract SPEC.md §122.

## The ask

The owner handed over three mockups of a "digital sound recorder" - a colour
face, the same face in black and white, and a shallow version for a
640 x 200 screen - and a written brief of thirteen user stories: record,
play, see and edit sound on an IBM XT-class machine, save WAV files, and do
it in a face that looks like a piece of 80s studio hardware. The brief asked
for the plan in a draft pull request before any code, and for that pull
request to be kept current as each story landed.

![The colour mockup](waverack/design/waverack-color.png)

## Wave 0 - finding out what the machine can already do

The first question was not "how do I draw a waveform" but "can this system
record at all". It can - SOUND.DRV's Sound Blaster tier has a record stream,
and an old package, the Recorder (§35), uses it - but only into ONE buffer of
the driver's staging pool, 20 KB at most: 1.9 seconds at 11,025 Hz. Worse,
the bytes reach that buffer through a kernel task, and a disk transfer
freezes every task for up to ~400 ms on an XT while the card's own ring is
372 ms deep. A recorder that writes to disk while it records would lose
audio on every write.

So the one change outside the package is a small one in the driver: let the
record stream capture straight into a ring the APPLICATION owns, the way the
Video Player's playback already plays out of one (§34.5.3). The card then
writes a 32 KB ring by DMA with nobody's help, and a 400 ms disk write is 4 KB
of a ring that holds three seconds.

The emulators set the limits of what can be checked here: QEMU's Sound
Blaster never raises a capture interrupt, and MartyPC's records a flat
mid-scale value. Both are useful - one for "no input", the other for rates,
counts and files - and neither is a microphone.
