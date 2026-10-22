# CD Player

CD Player plays an audio CD in the CD-ROM drive. The drive itself makes the
sound - through its front headphone socket, and through the sound card when
the drive's audio cable is connected to the card's CD input - so the
computer is free while a disc plays, and the music carries on whatever else
you open.

## Starting CD Player

Open the boot disk's `SYSTEM` folder and double-click **CDPLAYER.O88**. On
an audio CD, the CD-ROM drive lists one file per track, `TRACK01.CDA`,
`TRACK02.CDA` and so on; double-clicking one starts CD Player on that track.

CD Player needs [[CDROM.DRV]] loaded. The 720KB, 1.2MB and 1.44MB system
floppies and the live media carry it; the 360KB system floppy leaves it out,
because a CD-ROM drive needs a 286 or better.

## The display

The top line shows the track and how many there are, the time into the
track and the track's length. The second line says what the drive is doing
- **Playing**, **Paused**, **Stopped**, or **Drive error** when the drive
stopped a play itself - and the length of the whole disc.
Without an audio disc it says why: **No disc**, **Data disc - no audio
tracks**, **This drive plays no audio**, or **CD-ROM driver not loaded**.

## The buttons

- **|<** goes back a track. A few seconds into a track it starts that track
  again instead.
- **Play** plays from the track shown to the end of the disc, or carries on
  after a pause.
- **Pause** stops where it is; press it or Play to carry on.
- **Stop** stops.
- **>|** goes on a track. Stopped, the two arrows only choose the track that
  Play will start.
- **Eject** opens the drive's tray, and closes it again when the drive is
  empty.

A button that cannot do anything at the moment is grey. The keyboard works
too: **Space** plays or pauses, the **left** and **right arrows** change
track, **S** stops and **E** ejects.

## Good to know

- The volume of the drive's audio is set on the sound card's CD input, in
  the [[MIXER.O88]], or with the knob on the drive's front.
- Closing CD Player does not stop the disc; press Stop first.
