# Piano

Piano is a small keyboard you play with the mouse or the computer's keys.
It covers a little over an octave and a half, from middle C up to the G
above the next C. Every note you play is written on a staff at the top of
the window and remembered, so you can play your tune back. Three songs are
built in to listen to and play along with.

![Piano after a few bars typed on the keyboard, the notes written on the staff.](shot:piano-window){full}

## Starting Piano

Open the disk that holds it, then the `APPS` folder, and double-click
**PIANO.O88**.

Piano needs no sound card: the notes come out of the PC speaker, which
every machine has. If the sound driver ([[SOUND.DRV]]) is loaded for a card that can play
tones, the notes come from the card instead.

## Playing notes

Click a key with the mouse, or press the letter printed on it. The note
sounds and the key lights up while it plays: blue for a white key, red for
a black one.

| Keys | Notes |
|---|---|
| `a` `s` `d` `f` `g` `h` `j` `k` `l` `;` `'` `]` | the white keys, C D E F G A B C D E F G |
| `w` `e` `t` `y` `u` `o` `p` `[` | the black keys, C# D# F# G# A# C# D# F# |

The letters sit in a row on the keyboard the same way the keys do, so
`a` is the lowest C and `w`, just above it, is the C sharp beside it.

## The note viewer

The box at the top is a staff with five lines. Each note you play appears
as a dot at its height, with a short line through middle C and a small
sharp sign beside a black-key note. The viewer shows the last 21 notes;
older ones scroll off to the left.

## Recording and replaying

![A replay under way: the message says REPLAYING... and the sounding key is lit.](shot:piano-replay){right}

Piano records every note you play, with the time between notes. The
message to the right of the buttons says what is happening.

- **REPLAY** plays the recording back at the speed you played it, and the
  viewer fills in as it goes. A long pause between two notes is cut down
  to a little over a second.
- Click the mouse anywhere to stop a replay early. The message says
  **STOPPED**.
- **CLEAR** throws the recording away and empties the viewer.

The recording holds 300 notes. After that the keys still play, but the
message says **SEQ FULL** and nothing more is recorded until you press
CLEAR.

| Message | What it means |
|---|---|
| READY | Piano has just opened |
| REPLAYING... | A replay is playing |
| REPLAY DONE | The replay reached the end |
| STOPPED | You stopped the replay |
| CLEARED | The recording was thrown away |
| EMPTY | You pressed REPLAY with nothing recorded |
| SEQ FULL | 300 notes are recorded; new notes are not kept |
| TWINKLE READY, ODE READY, MARY READY | A song is loaded |

## The built-in songs

![Ode to Joy loaded and written on the staff.](shot:piano-song){left}

The **SONGS** row holds three buttons: **TWINKLE** (Twinkle Twinkle Little
Star), **ODE** (Ode to Joy) and **MARY** (Mary Had a Little Lamb). Clicking
one puts that song in place of your recording and writes its notes on the
staff. Press **REPLAY** to hear it.

Loading a song replaces whatever you had recorded. Notes you play after it
are added to the end of the song.

## The menus

![The Songs menu.](shot:piano-songs-menu){full}

The menus do the same as the buttons in the window.

| Menu | Item | What it does |
|---|---|---|
| Piano | About Piano | Shows who made it. Click or press a key to put it away |
| Piano | Close | Closes Piano |
| Play | Replay | Same as the REPLAY button |
| Play | Clear | Same as the CLEAR button |
| Songs | Twinkle | Loads Twinkle Twinkle Little Star |
| Songs | Ode to Joy | Loads Ode to Joy |
| Songs | Mary | Loads Mary Had a Little Lamb |

## Good to know

- Piano does not save anything. Closing it forgets the recording.
- While a replay plays, the rest of the desktop waits for it to finish.
  Click to stop it if you want to do something else.
- On a CGA screen the window has less room, so the keys are drawn
  shorter. They play the same.
- On a black-and-white screen the small sharp signs on the staff do not
  show.
