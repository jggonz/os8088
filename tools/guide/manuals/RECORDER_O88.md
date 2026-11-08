# Recorder

Recorder records a few seconds of sound from a Sound Blaster and plays it
back. The take is drawn as a wave in the strip at the top of the window, so
you can see what you recorded. A built-in demo sound lets you try playback
without recording anything.

## Starting Recorder

Recorder is in the `APPS` folder of the live USB stick and CD, and on no
floppy. Double-click **RECORDER.O88**; its icon is a microphone.

Recorder needs the sound driver, `SOUND.DRV` ([[SOUND.DRV]]), switched on
with a Sound Blaster under it. Recording needs a Sound Blaster with a DSP
of version 2.00 or later. Without the driver, every button that needs sound
answers **NO SOUND DRIVER** in the status line.

## The window

- **The wave strip** shows the take, one column per slice of sound, across
  the width of the strip.
- **The first status line** says what Recorder is doing, such as **READY**,
  **RECORDING...** or **PLAYING (SB)**, or why it refused a button.
- **The second status line** reads like `08000/40000 B  IN:SB`: how many
  bytes the take holds, how many it can hold on this machine, and the
  recording device (`SB`, or `--` when there is none).
- **Four buttons**: **REC**, **STOP**, **PLAY** and **DEMO**. A button acts
  when you let go of the mouse button over it, so you can slide off a
  button to change your mind. A button that cannot be used right now is
  greyed.

## Recording

Click **REC**. The status line reads **RECORDING...** and the byte count
climbs. Recording is at 8,000 samples a second. A take is up to 5 seconds
long; when the sound driver has less memory to give, Recorder asks for 3,
2 or 1 second instead and the second status line shows what it got.

Click **STOP** to end the take, or let it run until it is full, when the
status line says **REC STOPPED (FULL)**. Recording carries on while the
window is covered or minimized.

## Playing

Click **PLAY** to hear the take. With a Sound Blaster it plays in the
background and the status line reads **PLAYING (SB)**, then **PLAY DONE
(SB)**; you can keep using other programs, and **STOP** ends it early.
Without one it plays through the PC speaker. The desktop waits while it plays, a click stops it with **PLAY ABORTED**, and
the end reads **PLAY DONE (SPEAKER)**.

## The demo

Click **DEMO** to put a one-second test sound in place of the take: a tone
that rises from 400 Hz to 800 Hz and falls back. The status line reads
**DEMO STAGED - 400-800 HZ** and the strip shows its wave. Click **PLAY**
to hear it.

## The Recorder menu

| Item | What it does |
|---|---|
| About Recorder | Shows the About card, with who made Recorder. Click the window or pick a menu item to put it away |
| Close | Closes Recorder and stops anything it is playing or recording |

## The Sound menu

| Item | What it does |
|---|---|
| Record | Starts recording, like **REC** |
| Stop | Stops recording or playing, like **STOP** |
| Play | Plays the take, like **PLAY** |
| Demo | Puts the demo sound in place of the take, like **DEMO** |

A menu item picked at the wrong moment is refused with the same message as
its button.

## Messages

| Message | What it means |
|---|---|
| BUSY - STOP FIRST | Something is recording or playing. Stop it first |
| NOTHING RECORDED | There is no take to play yet |
| NOT RUNNING | **STOP** was chosen with nothing recording or playing |
| NO REC DEVICE (SB NEEDED) | Recording needs a Sound Blaster |
| NO SOUND DRIVER | The sound driver is off, or has no card under it |
| NO SOUND MEMORY | The sound driver or the machine had no memory to give |
| NO OUTPUT DEVICE | There is nothing to play the take on |
| SPEAKER PCM DISABLED | Sampled sound on the PC speaker is switched off |
| REC STOPPED (OVERRUN) | Recorder fell behind the card and stopped the take there |
| REC STOPPED (WATCHDOG) / PLAY STOPPED (WATCHDOG) | The card stopped answering, so the take or the playback was ended |

## Good to know

- Recorder keeps one take at a time, in memory. Recording again or
  choosing **DEMO** replaces it.
- Recorder does not open or save files. Closing the window throws the take
  away.
- Recorder has no keys.
