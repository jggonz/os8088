# XTMP3

XTMP3 plays MP3 files behind the face of an 80s stereo component: a logo
strip, a green LCD with the song's title and time, two VU meters, a volume
knob, the transport keys and a queue of songs. It works on every PC that
runs os8088, from a 486 down to the original 4.77 MHz IBM PC, and it always
tells you how well it is playing.

A fast computer plays a song at its own quality. A slower one plays it at
half or a quarter of its sample rate, which keeps the music going at the
cost of treble. When a computer cannot keep up at all, XTMP3 decodes the
whole song to a WAV file on the hard disk first, then plays that file. On an
IBM PC or XT that is how every song plays.

![XTMP3 on an IBM XT with After Hours loaded. The XT has no sound card, so the song is decoded with XT Standard before it plays.](shot:xtmp3-window){full}

## Starting XTMP3

XTMP3 is in the `APPS` folder of the 1.44MB and 1.2MB software disks. At
360KB it is at the root of the music disk. XTMP3 has a disk of its own in
every size, and the live USB image, the live CD and the everything set carry
it too. The 720KB software disk does not carry it. Use XTMP3's own disk at
that size.

Double-click **XTMP3.O88** to open it, or double-click any `.MP3` file to
open XTMP3, add that song to the queue and play it.

What XTMP3 needs depends on the computer:

- **A Sound Blaster** lets it play an MP3 as it decodes. An SB Pro adds
  stereo, and an SB16 adds 16-bit stereo. Switch the card on with
  [[SOUND.DRV]] in the Control Panel.
- **Without a card**, XTMP3 always decodes to the hard disk first. It then
  plays the WAV through the PC speaker, or through a Covox chosen on the
  Control Panel's Sound page.
- **A hard disk** is needed for decoding first. XTMP3 never decodes to a
  floppy.
- **Memory.** XT Standard needs 390KB, which a 640KB PC has. XT Fast needs
  360KB, XT Speech 340KB and XT Turbo 330KB. Original, Half and Quarter need
  220KB. When the computer has too little, the state line says what the
  quality needs and what the computer has, such as **Error: Needs 390 KB,
  has 300 KB**.

## Playing a song

Press `O`, or choose **File > Open...**, and pick an `.MP3` file. It joins
the queue and plays. The software disks carry three demo songs in `MEDIA`:
[[NEONDRV.MP3]], [[NIGHTSIG.MP3]] and [[AFTERHRS.MP3]].

The keys under the LCD work like a tape deck's: **Rewind**, **Play**,
**Pause**, **Stop** and **Fast Forward**, then **OPEN**. Rewind and Fast
Forward move ten seconds. A key that cannot do anything at the moment is
grey.

The LCD shows the song's number and title, the time played, a slash and the
song's length, a progress bar and the output format, such as **STEREO 44.1
kHz 16-BIT**. A length with a `~` in front is an estimate, because the file
does not say exactly how long it is. Click the progress bar to jump to that
point in the song.

Before a song plays, XTMP3 reads the whole file into memory when the
computer has room for it, and the state line says **Loading** with a
percentage. Playback then reads nothing from the disk, so a slow floppy
cannot interrupt the sound. Without the room, it plays from the disk as it
goes.

The first time a song plays, the state line says **Checking performance**
for a moment while XTMP3 measures how fast this computer decodes it. It
remembers the answer for that song until you close XTMP3.

## Quality

![The Options menu. Each item names its setting.](shot:xtmp3-options){right}

The status bar's left half says which quality is playing, such as
**Quality: HALF**. XTMP3 chooses it for you, starting at the best and
stepping down only as far as the computer needs.

| Quality | What you hear | Who it is for |
|---|---|---|
| Original | The song as it was made, in stereo where the card allows | A fast computer |
| Half | Half the sample rate: the top of the treble is gone | A computer a little too slow for Original |
| Quarter | A quarter of the rate: a radio-like sound | A computer slower again |
| XT Standard | A shortcut decoder built for slow PCs: 8-bit mono, close to any 8-bit player, about 11,000 samples a second whatever the song's rate | An XT, usually through Decode First, and a 386 or 486 playing a CD-quality song |
| XT Fast | Like XT Standard, a little rougher and a little quicker | An XT that needs the time |
| XT Turbo | Quicker still, with audible whistles on pure tones | Only when you choose it |
| XT Speech | Narrow, muffled sound, quick to decode | Spoken word on low-rate files, only when you choose it |

Automatic choice never lowers a song below 8,000 samples a second, so an
11 kHz song is never halved on its own. It asks you to decode first only
when no quality can play live on this computer.

**Q** or **Options > Quality** chooses a quality yourself. Each press moves
to the next one, and the item shows the current choice. A quality you chose
shows **(set)** after its name, and XTMP3 then never changes it. Back at
**Auto**, XTMP3 chooses again.

If a song starts to fall behind while it plays, XTMP3 steps down one quality
and carries on from the same place, with a short gap. It never steps back up
during a song.

Limits of the sound card are shown separately. When the card cannot play
the song's format, the LCD's format line adds **DEVICE**: an 8-bit card
plays 8-bit sound, whatever quality is chosen.

## Decode First

When no quality keeps up, or when you press `D` or choose **Playback >
Decode First...**, XTMP3 asks for a hard disk:

![Decode First on an XT with no hard disk, which it says, with the quality and the room the file needs.](shot:xtmp3-decode){full}

1. The window lists every hard disk with its free space. A disk that cannot
   be used is grey with its reason, such as **read-only** or **too little
   room**. Below the list are the quality, the sound the file will hold and
   the disk space it needs.
2. Choose a disk and click **Decode and Play**. The Save dialog opens on
   that disk. Pick a folder and a name and click **Save**. The name ends in
   `.WAV`.
3. A window shows the decoding: the time decoded so far against the song's
   length, the percentage, the time spent, and the speed, where **0.31x**
   means a second of music takes a little over three seconds. After ten
   seconds it adds how long is left, such as **About 12:40 left**, worked
   out from its own speed. The LCD and the state line follow it, once a
   second.
4. When it finishes, XTMP3 checks the file and plays it. The state line
   says **Playing decoded file**.

**Esc** or **Cancel** stops decoding at once and deletes the unfinished
file. Nothing else on the disk is touched. If the disk fills, the disk goes
away, or the MP3 changes while it is being decoded, decoding stops with the
reason and nothing plays.

On an IBM PC or XT at 4.77 MHz, decoding takes many times the song's own
length: about half an hour for a three-minute song at 11 kHz with XT
Standard, and hours for a CD-rate stereo song. Before the first song, an XT
also spends a few seconds checking it.

With more songs waiting in the queue, the switch **Queue too: No** /
**Queue too: Yes** decides whether the next songs are decoded into the same
folder without asking again.

## Decoded songs are kept

XTMP3 remembers every song it decodes. The next time you play that song, it
plays the finished WAV straight away, with no decoding. If the MP3 has
changed since, or the WAV is missing or damaged, XTMP3 decodes the song
again.

**File > Decoded Files...** lists the files XTMP3 has made. Choose one and
click **Delete** to remove it. XTMP3 deletes only files it made itself.

The finished file is an ordinary WAV, so the Audio Player
([[AUDIO.O88]]) and other computers play it too.

## Playing through the PC speaker or a Covox

Without a sound card, a decoded song plays through the PC speaker, the way
the Audio Player plays one. The speaker needs the whole computer, so the rest
of the desktop stops while it plays. Click or press `Space` to pause and
bring the desktop back. A Covox chosen in the Control Panel plays the same
way, with clearer sound.

## The meters

The two dials show the sound's level. XTMP3 never shows stereo that is not
there:

- With stereo output the dials are **L** and **R**.
- With mono output the left dial is **MONO**, and the right one becomes
  **BUFFER**: how far ahead of the music XTMP3 has decoded, or, while
  decoding first, how much of the song is done. Its green end is good.

While a song is decoded first, the MONO needle rests and BUFFER moves every
few seconds, so the decoding goes faster. **Options > Meters: Off** stops
the needles.

## The volume knob

Click the knob's left half to turn it down a step and its right half to
turn it up, or press `+` and `-`. On a card with a mixer the knob sets the
card's volume. Otherwise it sets the loudness of the decoded sound, and a
finished WAV on such a card cannot be turned up or down, so the knob is
grey.

## The queue

The queue holds 32 songs and shows each one's title and length. Titles
come from the song's own tag, or from its file name.

- Click a song to select it, and click it again to play it. `Up` and `Down`
  move the selection and `Enter` plays it.
- `Left` and `Right` play the previous and next song.
- The scroll bar and the mouse wheel move the list.
- **File > Add to Queue...** adds a song without playing it.
- **File > Remove from Queue** removes the selected song, and **File >
  Clear Queue** removes them all.

## Sizes and colour

XTMP3 picks the largest player the screen holds. VGA gets the full player
with three queue rows. EGA and Hercules get the same player shorter, with
two rows. CGA gets the compact player: the LCD, the keys and the state
line. **Options > Compact Player** (`C`) asks for the compact player on any
screen, and **Options > Full Player** goes back.

On EGA and VGA it opens in colour. Everywhere else it is black and white.
**Options > Theme** (`T`) switches between the two on a colour screen.

**Options > Status: Advanced** (`I`) shows figures in the state line
instead of the state: the most memory used, the number of gaps in the sound,
and the speed.

## The menus

| Menu | Item | Key | What it does |
|---|---|---|---|
| File | Open... | O | Adds a song to the queue and plays it |
| File | Add to Queue... | | Adds a song without playing it |
| File | Remove from Queue | | Removes the selected song |
| File | Clear Queue | | Empties the queue |
| File | Decoded Files... | | Lists and deletes the WAV files XTMP3 made |
| Playback | Play/Pause | Space | Plays, pauses, and carries on |
| Playback | Stop | S | Stops |
| Playback | Previous Track / Next Track | Left / Right | Plays the song before or after |
| Playback | Rewind / Fast Forward | , / . | Moves ten seconds back or forward |
| Playback | Decode First... | D | Decodes the song to the hard disk, then plays it |
| Options | Quality | Q | Steps through the qualities |
| Options | Theme | T | Colour or black and white |
| Options | Meters | | Turns the needles on or off |
| Options | Compact Player / Full Player | C | Changes the player's size |
| Options | Status | I | Basic or advanced state line |

About, in the XTMP3 menu, shows the program's card.

## Keys

| Key | What it does |
|---|---|
| Space | Play or pause |
| S | Stop |
| Left / Right | Previous / next song |
| , and . | Ten seconds back / forward |
| Up / Down | Move the queue selection |
| Enter | Play the selected song |
| + (or =) and - | Volume |
| O | Open a song |
| Q | Next quality |
| D | Decode First |
| T | Theme |
| C | Compact or full player |
| I | Basic or advanced state line |
| Esc | Cancel decoding |

## Files

- **Opens** MPEG-1, MPEG-2 and MPEG-2.5 Layer III files, the ordinary
  `.MP3`, at any bit rate from 8 to 320 kbps, constant or variable.
- **Writes** a `.WAV` in the folder you choose for each song it decodes.
- **Keeps** its list of decoded files, `XTMP3.DAT`, in the
  `SYSTEM\APPDATA` folder of the disk it was started from.

## Good to know

- An MP3 decoded live is never played through the PC speaker. Without a
  card, every song is decoded first.
- XTMP3 refuses MPEG Layer I and Layer II files, and "free format" MP3s, and
  says so by name. It also refuses an MP3 stored packed (**The MP3 is stored
  packed**).
- A hard disk partition of 1 to 2GB stores files in very large pieces.
  XTMP3 cannot play an MP3 from one (**Its disk's clusters are too large**);
  copy it to another disk first. It can play a decoded WAV from one. XT
  Standard cannot decode onto such a disk, so the Decode First list greys
  it with **32 KB clusters**; the other qualities can.
- Playback and seeking wait while a song is being decoded.
