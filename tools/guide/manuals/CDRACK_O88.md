# CDRack

CDRack plays an audio CD in the CD-ROM drive, behind the face of an 80s
rack CD player: an LCD with the track and the time, a grid of the disc's
tracks, picture buttons, a volume slider and the disc's track list. The
drive itself makes the sound - through its front headphone socket, and
through the sound card when the drive's audio cable is connected to the
card's CD input - so the computer is free while a disc plays, and the music
carries on whatever else you open.

## Starting CDRack

Open the boot disk's `SYSTEM` folder and double-click **CDRACK.O88**. On an
audio CD, the CD-ROM drive lists one file per track, `TRACK01.CDA`,
`TRACK02.CDA` and so on; double-clicking one starts CDRack on that track.

CDRack needs [[CDROM.DRV]] loaded. The 720KB, 1.2MB and 1.44MB system
floppies and the live media carry it; the 360KB system floppy leaves it out,
because a CD-ROM drive needs a 286 or better.

## Three sizes

CDRack picks the largest player the screen holds:

- **The full player** on a VGA screen: the tray, the LCD, the transport, the
  track list and a status bar along the bottom.
- **The same without the track list** on EGA and Hercules screens, which are
  shorter.
- **The compact player** on a CGA screen: the LCD, a progress bar, the
  buttons, the volume and Repeat in one small window. **Expand** goes back to
  the full player where it fits.

**Options > Compact Player** asks for the compact player on any screen, and
**Options > Full Player** goes back.

## Colour or black and white

On an EGA or VGA screen CDRack opens in colour - a dark deck with a cyan LCD.
Everywhere else it is black and white. **Options > Theme** switches between
the two; on a black-and-white screen the item is grey.

## The LCD

The LCD shows the track and the time into it, what the drive is doing -
**PLAYING**, **PAUSED**, **STOPPED** - and a grid of the disc's tracks, the
current one lit. Click a track in the grid to play it. Below, the time into
the track, a progress bar - click it to jump to that point - and the
track's length. Without an audio disc it says why: **NO DISC**, **DATA
DISC**, **NO AUDIO** (a drive that cannot play audio), or **NO DRIVE**
(CDROM.DRV is not loaded).

## The buttons

- **PREV** goes back a track. A few seconds into a track it starts that
  track again instead.
- **REW** and **FF** move ten seconds back or forward in the track.
- **PLAY** plays, or carries on after a pause. It stays lit while the disc
  plays.
- **PAUSE** stops where it is; press it or Play to carry on.
- **STOP** stops.
- **NEXT** goes on a track. Stopped, Prev and Next only choose the track
  that Play will start.
- **EJECT** opens the drive's tray, and closes it again when the drive is
  empty.

A button that cannot do anything at the moment is grey.

## Repeat, shuffle, program and time

- **REPEAT** goes back to the start when the disc - or the program, or the
  shuffle - is finished, instead of stopping.
- **SHUFFLE** plays the tracks in a random order, each once.
- **PROGRAM** lets you choose which tracks play: with it on, clicking a
  track in the grid or the list adds it to the program, or takes it out. A
  programmed track is yellow in the grid and marked with a star in the
  list, and Play then plays only those, in order.
- **TIME** shows the time left in the track instead of the time into it.

The same four are in the **Options** menu, where each one says whether it is
on.

## The volume

Drag the slider, or press **+** and **-**. Without a track list on screen the
mouse wheel turns it too. Some drives do not let a program set their volume; the
slider is grey there, and the knob on the drive's front or the
[[MIXER.O88]]'s CD level does the job.

## The track list

The full player lists every track with its length. Click one to play it;
the wheel and the scroll bar move the list.

## Keys

| key | does |
|---|---|
| Space | play or pause |
| Left, Up / Right, Down | previous / next track |
| , and . | ten seconds back / forward |
| S | stop |
| E | eject |
| + and - | volume |
| R, H, P, T | repeat, shuffle, program, time |
| C | compact or full player |

## Good to know

- Closing CDRack does not stop the disc; press Stop first.
