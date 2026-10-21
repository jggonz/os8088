# Mixer

Mixer adjusts the sound card's master volume and individual sources, such
as digital sound, FM music and CD audio. A stereo source has a balance
control; cards with bass and treble controls offer separate left and right
tone adjustments. The card determines which controls are available.

![Master volume in Mixer.](shot:mixer-window){full}

## Starting Mixer

Click the speaker beside the clock and choose **Open Mixer...**. This command
appears when **MIXER.O88** is in the boot disk's `SYSTEM` folder. You can also
open that folder and double-click the program. Several Mixer windows can be
open; they all control the same card and show its current settings.

Mixer needs [[SOUND.DRV]] loaded and a compatible Sound Blaster mixer.
The larger system floppies and live media carry it. The 360KB system floppy
leaves it out; its speaker still offers quick master volume. You can copy
MIXER.O88 into an installed system's SYSTEM folder.

## Choosing a source

Click a source in the left-hand list. Grey names are unavailable on this
card. **Master** controls the overall output. **PCM** controls digital sound;
**FM / MIDI** controls the card's music input. CD, Line, Mic and PC Speaker
adjust those inputs where the card offers them.

![PCM selected on a mono mixer.](shot:mixer-pcm){right}

Click a scale or drag its thumb. Changes take effect immediately. The
percentage reflects the nearest level the hardware can produce, so it can
move in fairly large steps on older cards.

For stereo sources, **Level** changes both sides together and **Balance**
moves the sound left or right. Equal sides put balance in the middle. A mono
source shows **Balance: mono**.

**Bass** and **Treble**, when present, show **Left tone** and **Right tone**.
Their flat setting is 53%; lower values cut and higher values boost.

## Keys

| Key | What it does |
|---|---|
| Up / Down | Select the previous or next available source |
| Tab | Switch between level and balance, or the two tone controls |
| Left / Right | Lower or raise the selected control |
| Home / End | Set the selected control to its minimum or maximum |

The `>` beside Level or Balance identifies the keyboard control. On a mono
source the keyboard stays on Level.

## The Mixer menu

| Item | What it does |
|---|---|
| About Mixer | Show the program's information card |
| Close | Close this Mixer window |

![Mixer's information card.](shot:mixer-about){left}

Any key or a click dismisses the information card.

## Good to know

Mixer writes no settings file. Closing its window leaves the card at the
chosen levels. Unloading the sound driver restores the settings it found
when it loaded. If the driver is unavailable, Mixer says **Mixer unavailable**
and directs you to load Sound on the Control Panel's Drivers page.

The PC Speaker input is the card's input from that speaker, where connected.
These controls do not change a built-in speaker or Covox independently of
the sound card.
