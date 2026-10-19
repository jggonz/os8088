# Tracker

Tracker plays Amiga music modules, the four-channel `.MOD` songs made with
ProTracker and the trackers that came after it. In a window it looks like a
pocket music player, with a green display, tape-deck buttons, a volume
slider and a level meter. Full screen, it becomes a pattern editor in the
style of FastTracker II, with every note scrolling past as it plays.

It keeps a playlist of modules and plays on in the background while you work
in other programs.

![Tracker playing Beverly Hills Cop in its window.](shot:tracker-window){full}

## Starting Tracker

Tracker is in the `APPS` folder of the software disks. Double-click
**TRACKER.O88** to open it empty, or double-click a `.MOD` file to open
Tracker and play that module. [[BEVERLY.MOD]], the Beverly Hills Cop theme, is
in the `MEDIA` folder of the 1.44MB, 1.2MB and 720KB software disks, and on
the media disk at 360KB.

Tracker plays through a Sound Blaster, with the sound driver ([[SOUND.DRV]])
loaded. With no card it plays through the PC
speaker, or through a Covox if one is chosen in the Control Panel (see
below).

## The window

The green display has four lines: the module's title; the time played and
the file name; the sample rate and the volume; and the song position, the
pattern, the speed, the tempo and the place in the playlist. The status line
at the bottom says what Tracker is doing and which key changes it.

The picture buttons along the middle are, from left to right:

| Button | What it does |
|---|---|
| Eject | Open a module (the same as **File > Open...**) |
| Previous | The previous module in the playlist |
| Rewind | Back one song position |
| Play | Play, or carry on after a pause |
| Pause | Pause where the music is |
| Stop | Stop and go back to the start of the song |
| Fast forward | Forward one song position |
| Next | The next module in the playlist |

The slider at the right of the buttons is the **volume**. Click or drag it,
and you hear the change as it moves. The long slider under the buttons shows
where you are in the song: drag it and let go to jump there.

The panel at the lower left is the **visualiser**, and the eight buttons
beside it are the options:

| Button | What it does |
|---|---|
| No Repeat / Rpt Song / Rpt List | Each click moves to the next. **Rpt Song** plays the module again and again. **Rpt List** goes round the whole playlist. **No Repeat** stops at the end of the list |
| Shuffle | Plays the playlist in a random order |
| PlayList | Opens and closes the PlayList editor |
| XT Mode | Turns XT mode on and off (see below) |
| VU Meter / Spectrum / Scope / Meter Off | Each click picks the next visualiser |
| The rate | Shows the sample rate and steps to the next one |
| Full Scrn | Full screen |
| About | Who made Tracker. Click or press a key to close it |

The **VU Meter** shows a needle for each of the four channels. The
**Spectrum** shows the notes being played as bars. The **Scope** draws the
sound wave itself. On an XT, the choice is VU Meter, a simpler Spectrum, or
Off.

## The playlist

![The PlayList editor with two modules.](shot:tracker-playlist){right}

Every module you open goes on the playlist. **File > Add to PlayList...**
adds one without playing it. When a module ends, Tracker moves on to the
next one, as the repeat button says.

**File > PlayList Editor**, the PlayList button or `E` opens the editor
beside the player. Click a module to select it, and click it again to play
it. The buttons are **Add...**, **Remove**, **Clear**, **Shuffle**,
**Sort** and **Play**. The playlist holds 16 modules, from any folders and
disks.

## Full screen

Press `F`, click **Full Scrn** or choose **View > Fullscreen**. The whole
screen becomes Tracker's, and the music keeps playing. Press `Esc` or `F` to
come back to the desktop.

There are two full screens:

- The **text screen** is 80 columns of text: the song position, tempo and
  speed at the top, a level bar for each channel, and the pattern of notes
  scrolling up past a highlighted line.
- The **graphics screen** is in the style of FastTracker II: the order list,
  tempo and pattern readouts, a meter for each channel, the button panel, the
  list of the module's instruments, and the pattern editor below.

![The text full screen, with the pattern scrolling past.](shot:tracker-textscreen){full}

**View > Text Screen** (or `V` in the window) picks which one `F` opens. In
XT mode the full screen is always the text screen, because an XT cannot draw
the graphics one fast enough.

![The graphics full screen. On an XT only the line being played is drawn while the song plays.](shot:tracker-fullscreen){full}

Keys `1` to `4` mute and unmute a channel, on the full screen and in the
window. Loading a module and the PlayList editor need
the window, so press `Esc` first. The playlist still plays on from one
module to the next.

## XT mode

XT mode makes Tracker playable on an IBM PC or XT. It plays at 5.5 kHz,
mixes the four channels in a cheaper way, uses the simple VU Meter, and
makes the full screen the text screen. It is on by itself on an 8088 or 8086
machine, and off on anything faster.

Turn it on or off with **File > XT Mode**, the **XT Mode** button or `X`.
Changing it stops the music first; press `Enter` to play on. It can only be
changed in the window.

## The Rate menu

The Rate menu chooses the sample rate. A higher rate sounds clearer and
takes more of the machine. The rate in use is marked with `*`, and changing
it stops the music first.

![The Rate menu in XT mode.](shot:tracker-rate-menu){left}

- Outside XT mode it lists **11 kHz**, **22 kHz**, **33 kHz** and **44 kHz**.
  Rates the card cannot play are left out; 33 and 44 kHz need a Sound
  Blaster Pro or Sound Blaster 16. A fast 286 is needed to mix 33 kHz, and a
  machine of 16 MHz or more for 44 kHz.
- In XT mode it lists **5.5 kHz** and **11 kHz (Windowed)**. An XT keeps up
  with 11 kHz only in the window, so at 11 kHz the full screen is greyed and
  there is no meter.

`R` steps through the same rates.

## Playing on the PC speaker or a Covox

With no Sound Blaster, **Play** plays through the PC speaker. That takes the
whole computer: the rest of the desktop waits while Tracker's own window
keeps playing. Click anywhere or press `Space` to pause and get the desktop
back. `Esc` or `S` stops, and `F` switches to the full screen.

- The first Play times the machine and picks the highest rate it can keep
  up with: 8,000, 5,512 or 4,800 Hz on an XT, and up to 22,050 Hz on a 286.
  The status line says what it picked and how busy the machine will be, for
  example **Spk 5512 Hz, 95% cpu**. Below 8 kHz it adds **(CARRIER
  WHINES!)**, because the speaker then makes a high whistle under the music.
- If even the lowest rate is too much, Tracker says how much of the PC it
  would need, such as **Speaker: needs 107% of this PC - Play again to try**.
  Press Play again and it plays anyway at the lowest rate.
- If the machine falls behind during a song, Tracker drops to a lower rate
  after a short gap and plays on.
- The volume slider becomes the speaker's **level**, from 0 to 10. Tracker
  picks one for you; move the slider, or press `+` and `-` while it plays,
  to choose your own.
- The Rate menu lists **Auto** and each rate the machine can play, with how
  busy each would make it.
- On a slow machine, loading a module says **Filtering the samples for the
  speaker...** for a moment while Tracker prepares its sound for the
  speaker.
- The visualiser is off on an XT while the speaker plays.

A Covox on the printer port plays the same way and sounds cleaner. Choose it
on the **Sound** page of the Control Panel first; Tracker then names Covox
wherever it would name the speaker.

## The menus

| Menu | Item | What it does |
|---|---|---|
| File | Open... | Load a module, add it to the playlist and play it |
| File | Add to PlayList... | Add a module to the playlist |
| File | PlayList Editor | Open or close the PlayList editor |
| File | XT Mode: Off / On | Turn XT mode on or off |
| View | Fullscreen | The full screen |
| View | Text Screen | Choose the text or the graphics full screen. Greyed in XT mode |
| Rate | the rates | The sample rate (see above) |
| Tracker | About Tracker | Who made Tracker |
| Tracker | Close | Close Tracker |

## Keys

| Key | What it does |
|---|---|
| `Enter` | Play, or carry on after a pause |
| `Space` | Pause and play |
| `Home` | Play from the start of the song |
| `P` | Play the current pattern over and over |
| `Left`, `Right` | Back or forward one song position |
| `Up`, `Down` | Scroll the pattern while stopped, on the full screen |
| `1` to `4` | Mute or unmute a channel |
| `N`, `B` | Next or previous module in the playlist |
| `+` (or `=`), `-` | Volume up or down |
| `O` | Repeat: No Repeat, Song, List |
| `H` | Shuffle on or off |
| `E` | The PlayList editor (window only) |
| `L` | Load a module (window only) |
| `F` | Full screen, and back |
| `V` | Text or graphics full screen (window only) |
| `X` | XT mode (window only) |
| `R` | The next sample rate |
| `Esc` | Leave the full screen |

## Files

Tracker plays four-channel ProTracker modules (`.MOD`). It reads the whole
module into memory before it plays, so modules up to 128 KB play, as long as
there is that much free memory. Tracker never writes to a module.

## Good to know

- A module bigger than the free memory is refused with **Too big for free
  memory**; close other programs and try again.
- On a CGA screen the window is shorter: the display has three lines and
  the Rate and About buttons move to the menus.
- Tracker plays Sound Blaster cards only. It does not play on an AdLib's FM
  chip.
