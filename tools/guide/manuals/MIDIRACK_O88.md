# MIDIRack

MIDIRack plays Standard MIDI Files, the `.MID` songs that keyboards and
sequencers write. Its window is laid out like a hi-fi rack: a playlist of
songs, a Now Playing display with the time and tempo, a Channel Rack that
shows what each of the sixteen MIDI channels is playing and how loud, and a
status pane at the bottom.

It plays through whatever sound hardware the machine has: the FM chip on an
AdLib or Sound Blaster, the Sound Blaster's sample output, a MIDI module on
an MPU-401, a Covox on the parallel port, or the PC speaker.

![MIDIRack playing a song on the FM chip, with its playlist and Channel Rack.](shot:midirack-window){full}

## Starting MIDIRack

MIDIRack is in the `APPS` folder of the 1.44MB, 1.2MB and 720KB software
disks. At 360KB it is on the media disk and on MIDIRack's own disk, at the
root of each. Double-click **MIDIRACK.O88**, or double-click any `.MID` file
to open MIDIRack and play it.

Opened on its own, MIDIRack looks for songs and lists them for you. It tries
a `MIDI` folder beside itself, then `\MEDIA\MIDI\`, then `\MIDI\`, then the
folder it was started from. The software disks carry ten demo songs in
`MEDIA\MIDI\`, such as [[FURELISE.MID]], so the playlist comes up full and the first song is loaded,
ready to play. The Playlist pane's title shows the folder they came from.

MIDIRack needs no sound card: with none, it plays through the PC speaker.
A card is reached through [[SOUND.DRV]], switched on in the Control Panel.

## Playing a song

- Click a song in the playlist to highlight it, and click it again quickly
  to play it. You can also move the highlight with the `Up` and `Down` keys
  and press `Enter`.
- The **Play**, **Pause**, **Stop**, **Prev** and **Next** buttons under Now
  Playing work as on a tape deck. `Space` plays and pauses.
- **Prev** goes to the song above. On the first song it starts that song
  again. **Next** goes to the song below. When nothing is playing, they only
  move the highlight.
- **Eject** stops and unloads the song, and clears the Channel Rack.
- **Loop** (or the **Loop Playback** check box) plays the same song again
  from the start when it ends. With Loop off, MIDIRack moves on to the next
  song in the list, and says **End of playlist** after the last one.

Now Playing shows the song's name, its length, the time played so far, the
measure it has reached, the tempo, whether it loops, and the output it is
playing on. The bar under it fills as the song plays. You cannot click it to
jump to another place in the song.

## The Channel Rack

The Channel Rack has a row for each of the sixteen MIDI channels. **Lev** is
a bar that moves with how loudly the channel is playing, and the rest of the
row names its instrument (its General MIDI program number and name).
Channel 10 is the drum channel and shows **Drum Kit**.

A stopped song still shows the instruments it starts with. Turn the level
bars off with the **Show Levels** check box or **Options > Show Levels**.

## Building a playlist

- **Add** (or **File > Add...**) adds one file you pick in the Open dialog.
- **Load Directory** (or **File > Load Directory...**) adds every `.MID` and
  `.SMF` file in the folder you leave the dialog in.
- **Remove** takes the highlighted song off the list. If it is the song
  playing, it stops.
- **File > Clear Playlist** empties the list.

The playlist holds 64 songs, and they can come from different folders and
disks.

## Choosing the output: Settings

**Settings** (the button, or **Options > Settings...**) opens a dialog over
the window. Its top line, **Found:**, names the sound hardware MIDIRack
found in this machine.

![The Settings dialog, with the output device and the sample rate.](shot:midirack-settings){full}

**Output device** picks where the music goes. Anything the machine does not
have is greyed.

| Output | What it is |
|---|---|
| Automatic | The best the machine has: an FM chip first, then the Sound Blaster synth, then a Covox, then the PC speaker |
| OPL3 FM, 17 voices | The stereo FM chip on a Sound Blaster Pro 2 or Sound Blaster 16 |
| OPL2 FM (AdLib), 8 | The FM chip on an AdLib or any Sound Blaster |
| Sound Blaster synth | MIDIRack's own synthesiser, played through the Sound Blaster's sample output |
| PC speaker synth | The same synthesiser, played through the PC speaker |
| MIDI out (MPU-401) | Sends the song to a MIDI module or keyboard on an MPU-401 interface, which makes the sound |
| SB wavetable (bank) | Real instrument samples, mixed by the computer and played on the Sound Blaster. It needs the file [[MIDIRACK.BNK]] beside MIDIRack |
| Covox (parallel port) | MIDIRack's synthesiser played on a Covox sound device on the printer port |

The FM chip and MIDI out cost the computer almost nothing while they play.
The Sound Blaster synth and the wavetable keep your other programs running
too, but they use more of the machine. The wavetable is the heaviest: an XT
plays it with three voices, so choose it on a faster machine.

**Synth sample rate** sets the quality of the Sound Blaster synth, the
wavetable, the speaker and the Covox. **Automatic** picks a rate that suits
the computer. Rates above 22,050 Hz need a Sound Blaster Pro or Sound
Blaster 16. The FM chip and MIDI out have no rate.

**Speaker: play the melody in the background** changes how the PC speaker
and the Covox play (see below). It is the same setting as **Options > Play
in Background**.

**OK** (or `Enter`) keeps your choices. If a song is playing or paused, it
starts again on the new output. **Cancel**, `Esc` or a click outside the
dialog changes nothing.

MIDIRack remembers the output, the rate, Play in Background, Loop and Show
Levels for next time.

## Playing on the PC speaker or a Covox

The PC speaker can play the whole song, chords and drums included, but it
needs the whole computer to do it. While it plays, the rest of the desktop
stops and the pointer disappears. Only MIDIRack's clock, bar, measure and
Channel Rack keep moving, and the status line says **PC speaker - click or
Space to pause**.

- Click anywhere or press `Space` to pause. The desktop comes back, and
  `Space` or **Play** carries on from where it stopped.
- `Esc` or `S` stops.
- `N` and `P` skip to the next or previous song without stopping.

A Covox plays the same way, and sounds cleaner. A Covox cannot be detected,
so you first choose it on the **Sound** page of the Control Panel; until then
its row in Settings is greyed.

With **Play in Background** ticked, the speaker plays only the melody, as a
single tone, and the desktop stays usable while it plays.

## MIDI Info

![MIDI Info for the loaded song.](shot:midirack-info){full}

![The Options menu.](shot:midirack-options-menu){right}

**MIDI Info** (the button, **Options > MIDI Info...** or `I`) shows details
of the loaded song: its file size, the MIDI format and number of tracks, its
timing, its length in time and measures, how many notes it has on how many
channels, and the first text written in the file, which is often its title.
Click or press any key to close it.

**About** (or **Options > About MIDIRack**) shows who made MIDIRack, and
closes the same way. **Quit** (or **File > Quit**) closes MIDIRack.

## The menus

| Menu | Item | What it does |
|---|---|---|
| File | Add... | Add a song to the playlist |
| File | Load Directory... | Add every song in a folder |
| File | Remove | Remove the highlighted song |
| File | Clear Playlist | Empty the playlist |
| File | Quit | Close MIDIRack |
| Play | Play, Pause, Stop | The same as the buttons |
| Play | Previous, Next | The song above or below |
| Play | Loop | Play the same song again when it ends |
| Options | Settings... | Choose the output and the sample rate |
| Options | MIDI Info... | Details of the loaded song |
| Options | Show Levels | Turn the Channel Rack's level bars on and off |
| Options | Play in Background | The speaker plays the melody only, and the desktop stays usable |
| Options | About MIDIRack | Who made MIDIRack |

## Keys

| Key | What it does |
|---|---|
| `Space` | Play or pause |
| `Enter` | Play the highlighted song from its start |
| `Up`, `Down` | Move the highlight in the playlist |
| `Left` or `P` | Previous song |
| `Right` or `N` | Next song |
| `Esc` or `S` | Stop |
| `L` | Loop on or off |
| `I` | MIDI Info |

In the Settings dialog, `Enter` is **OK** and `Esc` is **Cancel**.

## Files

MIDIRack plays Standard MIDI Files of type 0 and type 1, named `.MID` or
`.SMF`, up to 63 KB each. It reads the whole song into memory before it
plays, so the disk is quiet during the music.

Its settings are kept in `MIDIRACK.CFG`, in the `SYSTEM\APPDATA` folder of
the disk MIDIRack is on.

The ten demo songs are four originals (INTRO, SPACEJAM, BATTLE1 and DEMO)
and arrangements of six classics: The Entertainer, Fur Elise, In the Hall of
the Mountain King, Korobeiniki, the Can-can and Rondo alla Turca.

## Good to know

- MIDIRack refuses a MIDI file of type 2, and a file timed in SMPTE frames,
  and says so on the status line.
- The wavetable bank is on MIDIRack's own disk only. Without it the
  wavetable row is greyed.
- MIDI out and the wavetable are never picked by Automatic. Choose them in
  Settings.
- MIDIRack does not play General MIDI level 2 extras, aftertouch or the FM
  chip's drum mode.
- On a CGA screen the window is more compact: the Status pane and the bottom
  row of buttons are left out, and everything they do is in the menus.
