# Sound Debug

Sound Debug is a report on your sound card, written for sending to the
os8088 project when sound does not work the way it should. It shows where
the sound driver found each card, what it learned about them, and what
happens when it plays a tone for ten seconds. Everything is plain text on
three pages, so a photograph of the screen carries all of it.

## Starting Sound Debug

Sound Debug comes on the sound debug disk, in the `DEBUG` folder of the
release. Boot that disk in drive **A:** and Sound Debug opens by itself as
soon as the machine has started. It needs nothing else: the disk switches
the sound driver on even when the machine would not have done so on its
own. Its own page, `SNDDEBUG.TXT`, is at the top of the disk.

The detail comes from the sound driver on that disk. Started from another
disk, Sound Debug says the driver is not the debug one and shows only what
any sound driver reports.

## What to send

1. Boot the disk and listen. Two seconds after Sound Debug opens it plays a
   steady tone for ten seconds.
2. Note whether you heard the tone the whole time, and if not, when it
   stopped.
3. When page 2 says **Done.**, photograph the screen. Press **1** and
   photograph page 1, then press **3** and photograph page 3.
4. Send the three photographs, what you heard and the name of your card.

## The pages

| Page | Key | What it shows |
|---|---|---|
| Detect | 1 | the processor and display, whether the sound driver is loaded, what it can play, where the Sound Blaster answered and its version, how its interrupt was found, the mixer, an ESS chip's own name, the FM chip, MIDI port, Covox and PicoMEM, a `BLASTER=` line, and a one-line verdict |
| Play test | 2 | the tone, one line a second: the bytes the card played, its interrupts, whether the card's own output switch is on (`D8h`: `FF` on, `00` off), the mixer settings that can silence it, and the playback state, then a verdict naming the first thing that stopped |
| Registers | 3 | the mixer's registers, the last commands sent to the card, and the playback and interrupt-controller state |

## Keys

| Key | What it does |
|---|---|
| 1, 2, 3 | show that page |
| P | play the test again |
| R | change the test's rate: 22,050, 11,025, 8,000 or 44,100 Hz |
| V | stop or start reading the card's output switch during the test |
| Esc | close Sound Debug |

## Good to know

- `na` in the `D8h` column means the card did not answer that question;
  some cards never do. `--` means it was not asked.
- If the tone stops while the `D8h` reading is on and plays through with it
  off (**V**, then **P**), say so: that is a finding too.
- 44,100 Hz needs a Sound Blaster Pro, an SB16 or a compatible; on another
  card the test says it was refused.
