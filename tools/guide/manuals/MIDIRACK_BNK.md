# MIDIRack instruments

`MIDIRACK.BNK` is MIDIRack's wavetable bank: a recording of a real
instrument for each of the 128 General MIDI instruments and 61 drum sounds,
160 samples in all, about 206KB. With it, a Sound Blaster plays MIDI songs
with sampled pianos, strings and drums in place of the FM chip's
synthesised tones.

![MIDIRack's Settings, where Found lists the Bank and SB wavetable (bank) can be chosen.](shot:midirack-settings)

## Where it comes from

The sounds are taken from GeneralUser GS 2.0.3, a free General MIDI sound
set by S. Christian Collins, whose licence allows it to be used and changed
in software projects. Each instrument was cut down to one sample that fits
an XT's memory: the note nearest middle C, at a moderate loudness and at
most 11,025 samples a second.

## Where it is

At the top of MIDIRack's own disk in all four sizes, beside `MIDIRACK.O88`,
and in the `APPS` folder of the first disk of the everything set. It must
stay in the same folder as MIDIRack. The software disks and the music disk
carry MIDIRack without it, and there the wavetable choice is greyed.

## Using it

Open MIDIRack ([[MIDIRACK.O88]]) and click **Settings**. The **Found** line names **Bank** when
the file is beside MIDIRack. Choose **SB wavetable (bank)** under **Output
device** and press **OK**. It needs a Sound Blaster, and Automatic never
picks it: you choose it yourself.

The first song played this way reads the whole bank into memory, which
takes a while from a floppy, and it stays there until you close MIDIRack.
The wavetable is the heaviest of MIDIRack's outputs: an XT plays three
voices at a time, a 286 eight and a 386 sixteen, so it sounds its best on a
faster machine.
