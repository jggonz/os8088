# Audio Player

Audio Player plays `.WAV` sound files. It reads a song from the disk a
little at a time as it plays, so even a long recording needs very little
memory, and with a Sound Blaster the music carries on in the background
while you work in [[NOTEPAD.O88]], [[SHEET.O88]] or any other program.

It keeps a playlist of up to 48 songs and can play them in order, shuffled,
or round and round.

![Audio Player playing a song.](shot:audio-window){full}

## Starting Audio Player

Audio Player is in the `APPS` folder of the software disks. Double-click
**AUDIO.O88** to open it with an empty playlist, or double-click any `.WAV`
file to open Audio Player and play that file at once.

Each double-click on a `.WAV` opens a new Audio Player window. To queue
several songs in one player, use **File > Open...** instead.

Audio Player plays through a Sound Blaster, using [[SOUND.DRV]]. With no card it plays through
the PC speaker, or through a Covox if one is chosen in the Control Panel
(see below).

## Playing

The window shows the song's name, what the player is doing, the time played
and the song's length either side of a progress bar, a row of buttons and
the playlist. The `>` mark in the playlist is the current song.

| Button | Key | What it does |
|---|---|---|
| Prev | `P` or `Left` | The song before. From the first song it goes round to the last |
| Play | `Space` or `Enter` | Play the current song, or carry on after a pause |
| Pause | `Space` or `Enter` | Pause where the music is |
| Stop | `S` | Stop, and go back to the start of the song |
| Next | `N` or `Right` | The next song |
| Shuf | | Shuffle: play the playlist in a random order |
| Rep | | Repeat: after the last song, start the playlist again |

![Paused on the second song, with Repeat on.](shot:audio-paused){right}

`Space` and `Enter` switch between playing and pausing. **Shuf** and **Rep**
stay pressed in while they are on.

When a song ends, the next one starts. After the last song the player stops
and says **End of playlist**, unless Repeat is on.

## Building a playlist

**File > Open...** shows the Open dialog. Pick a `.WAV` file and it is added
to the end of the playlist.

![Choosing a song in the Open dialog.](shot:audio-open){left}

- If a song is playing or paused, it is left as it is, and the new one
  waits its turn at the end of the list. Play goes on with the paused song.
- If nothing is playing, the new song becomes the current one and starts
  straight away.
- A file that is already on the list is not added twice; it is selected
  instead.

**File > Clear playlist** stops the music and empties the list.

The playlist remembers which folder and disk each song came from, so one
list can hold songs from several places.

## Playing on the PC speaker or a Covox

![The Play menu.](shot:audio-play-menu){left}

With no Sound Blaster, Audio Player plays through the PC speaker. The
speaker needs the whole computer to play a recording, so while it plays the
rest of the desktop stops and the pointer disappears. Only Audio Player's
clock and progress bar keep moving, and the status line says **PC speaker -
click or Space to pause**.

- Click anywhere or press `Space` to pause. The desktop comes back.
  `Space`, `Enter` or **Play** carries on from exactly where it stopped.
- `Esc` or `S` stops.
- `N` or `Right`, and `P` or `Left`, change song without leaving the play.
- The whole playlist plays this way, one song after another.

Audio Player plays the speaker at the best rate the machine can manage. If
the machine falls behind, there is a short gap of about a second while it
moves to a lower rate, and then it keeps time. An ADPCM file that is too
much for the machine even then stops with **Too slow for this file on the
speaker**.

A Covox on the printer port plays the same way, with the clearer sound of a
real sound device. A Covox cannot be detected, so you choose it on the
**Sound** page of the Control Panel. The messages then say Covox where they
would say speaker.

## The menus

| Menu | Item | What it does |
|---|---|---|
| File | Open... | Add a `.WAV` file to the playlist |
| File | Clear playlist | Stop and empty the playlist |
| File | Diagnostics (D) | Always greyed; it is for testing builds only |
| File | Exit | Close Audio Player |
| Play | Play | Play, or carry on after a pause |
| Play | Pause | Pause |
| Play | Stop | Stop |
| Play | Previous | The song before |
| Play | Next | The next song |
| Play | Shuffle | Shuffle on or off |
| Play | Repeat | Repeat on or off |
| Audio Player | About Audio Player | Who made Audio Player |

![The About card.](shot:audio-about){right}

**About Audio Player** shows a card with the version, the formats and the
rates. Click or press a key to close it.

## Files

Audio Player plays mono `.WAV` files in two formats:

- **8-bit PCM**, the plain format most sound programs can save.
- **IMA ADPCM** (also called DVI ADPCM), a 4-bit compressed format that
  makes the file a quarter the size.

The sample rate can be 8,000, 11,025, 16,000 or 22,050 Hz on any Sound
Blaster. A Sound Blaster Pro or Sound Blaster 16 also plays 24,000, 32,000
and 44,100 Hz; on an older card those say **Rate needs a Sound Blaster
Pro**.

Audio Player never writes to a file.

## Good to know

- Stereo and 16-bit files are refused with **Only mono WAV is supported**
  or **Unsupported bit depth**. Convert them to 8-bit mono first. Other
  problems with a file are named on the status line in the same way.
- You cannot click the progress bar to jump within a song, and clicking a
  line of the playlist does nothing. Use **Prev** and **Next** to move
  between songs.
- **No free task - close an app and retry** means too many programs are
  running for the player to start; close one and press Play.
- A WAV made for the speaker on a faster machine may say **Made for a
  faster machine**.
- On an XT, 8,000 Hz files are the best choice for the speaker. A 22,050 Hz
  file plays, but at a lower rate.
