# Video Player

Video Player plays `.V88` videos, with sound where the machine has a way to
play it. A video plays in its window or on the whole screen, and you can
move between the two while it plays. The window shows a still picture from
the video, a bar to pick where to start, and a row of buttons, with an
information card beside it when you want one.

![The os8088 logo video playing in its window, on the desktop.](shot:video-window){full}

Videos are made on a modern computer with the os8088 video encoder, a
separate program that turns an ordinary video file into a `.V88`. Its
page in this guide is **Video encoder**, under Tools.

## Starting Video Player

Double-click a `.V88` file and Video Player opens with it. You can also
open the `APPS` folder and double-click **VIDEO.O88**, then pick a video
with **File > Open...** or the eject button.

One video comes with os8088: [[OS8088.V88]], a short looping animation of
the os8088 logo, in the `MEDIA` folder of the 720KB, 1.2MB and 1.44MB disks
and of the 360KB media disk.

The window's title names the video, for example **Video Player - os8088**.

## The window

From top to bottom the window holds:

- **The picture box**, showing a still from the video. When a video opens
  this is the picture its maker chose; after a play it is the frame where
  the next play will start.
- **The bar**, with a black marker where Play will start. Click the bar or
  drag the marker to choose a place. When you let go, the marker moves to
  the nearest starting point before it. A video can only be started from
  these points (the video's keyframes), so the bar moves in steps.
- **The buttons**, from left to right:

| Button | What it does |
|---|---|
| Repeat (two arrows in a loop) | Turns Repeat on and off. It stays pressed in while Repeat is on |
| Mute (a speaker) | Turns the sound off and on. It stays pressed in while the sound is off, and is greyed for a video with no sound |
| Eject | Opens a different video |
| Previous key | Moves the start back one step |
| Play | Plays the video. While a video plays in the window it shows Pause |
| Next key | Moves the start on one step |
| `i` | Shows or hides the information card |

At the first step Previous key is greyed, and at the last step Next key is.
On a screen too small for the row of buttons under the picture, they move
into the information card, and the card stays open.

On a 286 or faster machine the picture follows the marker while you drag
it. On an 8088 the picture changes when you let go.

## Playing a video

Press **Space**, **P** or **Enter**, click **Play**, or choose **File >
Play**. The video plays from the place the marker shows.

Where the picture can play in the window, it does: the picture is at the
video's own size, nothing covers the window, and the whole picture is on
the screen. Otherwise the video plays on the whole screen.

While a video plays in the window, the rest of the desktop stands still and
the mouse pointer disappears. Press **Space** or click the mouse to pause;
the desktop comes back with the video paused in its box. Press Space again
to play on. A click on the Repeat or Mute button turns that setting over
instead, and the video plays on. R, M, F and Esc work as they do on the
whole screen.

When a play ends, Play next starts where it got to: at the starting point
at or before the last frame shown. A video that played to the end starts
from the beginning again.

## Live play on the desktop

Some videos are made to play **Live**: they play in the window while the
rest of the desktop keeps working. You can move the mouse, use the menus,
drag the window and work in other windows while the video plays. The logo
video is one of these.

A Live video is read into memory before it starts, which takes a few
seconds from a floppy. It then plays with no disk use at all. Live play
needs the picture at its own size in the window and a black-and-white
video, or a 16-colour one on a VGA or EGA. Space pauses and resumes it, and
**F** takes it to the whole screen and back without stopping.

## The whole screen

Press **F** or **Alt+Enter**, or choose **File > Full screen**, to show the
picture on the whole screen. It goes there paused; press Space to play.
F, Alt+Enter or Esc bring it back to the window.

![On the whole screen, paused. The word Paused sits in the corner of the picture.](shot:video-fullscreen-paused){full}

| Key | On the whole screen |
|---|---|
| Space | Pause and play again. **Paused** shows while it is paused |
| Esc | Stop the video and go back to the window |
| F or Alt+Enter | Go back to the window, still playing or paused |
| Left, Right | Go back or forward 5 seconds a press |
| Home | Go back to the start |
| R | Turn Repeat on or off. The screen says **Repeat on** or **Repeat off** |
| M or S | Turn the sound off or on. The screen says **Sound off** or **Sound on** |

The first press of Left or Right pauses the video and shows where it will
land, such as `>> 1:05` or `<< 0:40`. Press again to go further. Half a
second after your last press the video jumps to the starting point at or
before that time, and plays on if it was playing. A video with no starting
points says **Seek: no keyframes**.

Some videos only play on the whole screen: 256-colour videos, colour CGA
videos, and videos made of text characters.

## Keys in the window

| Key | What it does |
|---|---|
| Space, P or Enter | Play, or pause a play in the window |
| F or Alt+Enter | The whole screen, paused |
| Left, Right | Move the start back or on one step |
| Esc | Stop a paused play, keeping its place |
| I | Show or hide the information card |
| R | Turn Repeat on or off |
| M or S | Turn the sound off or on |

## The File menu

![The File menu.](shot:video-file-menu){full}

| Item | Key | What it does |
|---|---|---|
| Open... | | Opens a different video |
| Play | Space | Plays the video |
| Full screen | F | Shows it on the whole screen, paused |
| Previous key | Left | Moves the start back one step |
| Next key | Right | Moves the start on one step |
| Info | I | Shows or hides the information card |
| Repeat | R | Turns Repeat on or off |
| Mute | M | Turns the sound off or on |

The **Video Player** menu holds **About Video Player** and **Close**.

## Repeat

With Repeat on, a video starts again from the beginning when it reaches the
end, with no pause between, until you stop it. A video can be made with
Repeat already on; the logo video is. You can turn Repeat off during a
play, and the video then stops at the end of the time round.

## The information card

The card beside the picture says, line by line:

- the file's name, and the video's title;
- the picture's size and the screen it was made for, the frame rate and
  how long it runs;
- how much it reads from the disk each second, and its sound: `silent`,
  `sound PCM8` or `sound ADPCM4`, and how it will play (`speaker`,
  `Covox`, `muted`);
- where Play will start, such as `At key 3 of 110, 0:04`, or, while a play
  is paused, where it stopped, such as `Paused at 0:04`;
- what Play will do, or why it cannot play here;
- after a play, how it went: frames drawn (`Drew 105/105`), stalls (frames
  that waited for the disk), and the time it took against the time it
  should take, in ticks of about 1/18 second.

![The information card beside the picture.](shot:video-info){full}

The stalls are the answer to "is this disk fast enough for this video?". A
play with no stalls kept up the whole way. The card comes out by itself
when a video cannot play, to say why.

## Sound

A video has no sound, 8-bit sound (PCM8) or 4-bit sound (ADPCM4).

- **With a Sound Blaster**, and the sound driver ([[SOUND.DRV]]) loaded from
  the Control Panel's Sound page, both kinds play through the card. Live videos play
  their sound only on a Sound Blaster.
- **With no sound card**, 8-bit sound plays through the PC speaker. On an
  8088 a video whose sound is faster than 8,000 samples a second opens
  muted, because the speaker would leave too little time for the picture;
  the card says `muted: fast`. Press M to try it anyway. Videos made for
  the speaker play best this way.
- **With a Covox** chosen on the Control Panel's Sound page, the sound goes
  to the Covox instead of the speaker.
- **4-bit sound** needs the Sound Blaster. On a card that may not handle
  it, the video opens muted and the card says `muted: DSP 4`; press M to
  turn it on.

Muting takes effect at once. Turning the sound back on during a play moves
the play to the starting point at or before where it was, so the sound and
picture start together.

## Files

Video Player opens `.V88` files. It does not write any files.

A video is made for one kind of screen: CGA, Hercules or VGA. On another
screen it plays through a copy, a little slower, and the card says so, for
example `Made for VGA: plays via a copy`. A video the screen cannot show at
all greys Play and the card says why, such as `256 colours: a VGA, full
screen` or `Colour text: needs CGA, EGA or VGA`.

## Good to know

- A video needs memory to play. If there is not enough, the card says
  `Not enough memory to play it`, or how much it needs and how much is
  free. Close other programs and try again.
- On a machine with extended (XMS) or expanded (EMS) memory, the player
  keeps part or all of a long video there, ahead of the disk, which helps a
  slow disk keep up.
- A copy that was cut short or damaged says `This .V88 is damaged`, or
  stops part way with `Stopped: a damaged frame`.
- A video longer than 65,535 frames cannot be played.
- On the whole screen the mouse does nothing; use the keys.
