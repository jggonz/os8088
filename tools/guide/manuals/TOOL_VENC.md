# Video encoder

The video encoder runs on your modern computer, not on os8088. It turns an
ordinary video file (an MP4, an MKV, anything ffmpeg can read) into a `.V88`
file that Video Player ([[VIDEO.O88]]) plays on os8088. It fits every frame to the machine
the video is made for: how fast its disk reads, how much of the processor
the picture may use, and what its screen can show.

There are two ways to use it. **os88vencgui.py** opens a window with every
setting on tabs, a preview of the encoded frames as the target screen will
show them, and a button that puts the result on a disk image.
**os88venc.py** is the same encoder on the command line. The window runs the
command-line encoder underneath, so both make the same file from the same
settings.

The encoder can also save the sound of a video alone, as a `.WAV` shaped
for the PC speaker, for Audio Player to play.

## What it needs

- **Python 3.** The window also needs Tk. The python.org installer
  includes it; on Linux install the `python3-tk` package.
- **Two Python packages**, numpy and Pillow:
  `python3 -m pip install numpy pillow`
- **ffmpeg and ffprobe** on your PATH, from ffmpeg.org. They read your
  video.
- Optional: the `tkinterdnd2` package, so you can drop a file on the
  window on any system. On Windows a drop works without it.
- Optional: `tesseract` on your PATH, for the text format's word reading
  (the **Text mode: words read (OCR)** group).

## Getting the encoder

In an os8088 source tree the two programs are `tools/os88venc.py` and
`tools/os88vencgui.py`. Start the window with
`python3 tools/os88vencgui.py`.

Without the source tree, use the encoder bundle, `os8088-encoder.zip`. It
holds one folder, `os8088-encoder`, with the window, everything it needs,
Video Player (**VIDEO.O88**), the files a bootable hard disk needs, and a
`README.TXT`. Unzip it anywhere and run `python3 os88vencgui.py` inside that
folder. Someone with the tree makes the bundle with `make vencbundle`.

## Making a video in the window

1. Choose your video. Click **Browse...** beside **Input**, or drop the
   file on the window. The line under **Made for** then says what ffprobe
   found in it: its frame rate, its length and whether it has sound.
2. Choose what it is for in **Made for**: the machine and the screen it
   will play on. This sets the preset, the picture format and the machine
   profile together, and fills in every setting they imply. You can change
   any of them afterwards on the tabs.
3. Check **Save as**. It starts as the video's own name with `.V88`,
   beside the video. The box at the end of the line chooses between a
   `.V88` video and a `.WAV` of the sound alone for Audio Player.
4. Tick **...and make a disk of it** and pick a disk if you want the video
   on a disk image as well (see below).
5. Click **Encode**. Under the progress bar the encoder says what it is
   doing: which pass it is on and why, the step with its own count (frames,
   scenes or trials), how long the step has taken and about how long it
   has left, and anything running beside it. A step that has nothing to
   count, such as making the palette, shows a moving bar instead. Some
   options make the encoder run more than one pass; each starts the bar
   again. **Cancel** stops it; nothing is written, and a file it would
   have replaced is left as it was.

The **Made for** list:

| Made for | What you get |
|---|---|
| IBM 5150/XT, CGA - black and white | Plays in a window on a CGA desktop |
| IBM 5150/XT, CGA - 4 colours, full screen | CGA's four-colour mode |
| IBM 5150/XT, CGA - 16 colours at 160 x 100, full screen | Sixteen colours on CGA's text-mode trick |
| IBM 5150/XT, CGA on a composite monitor - colour | Colour from a composite monitor |
| IBM 5150/XT, CGA on a composite monitor - 512 colours, full screen | About 450 colours, on a real CGA only |
| Any PC - text mode, 16 colours, full screen | The picture drawn in text characters |
| Any PC, Hercules and MDA too - text mode, black and white, full screen | The same in grey, white and reverse |
| IBM 5150/XT, Hercules - black and white | Plays in a window on a Hercules desktop |
| IBM 5150/XT, PC speaker - Hercules (preset cga-spk: CGA) | Sound on the PC speaker, for a machine with no sound card |
| IBM 5150/XT, a floppy - small and slow | A small picture that plays off a floppy |
| 286, VGA - 16 colours in the window | Plays in a window on a VGA desktop |
| 286, VGA - 256 colours, full screen | 320 x 200 in 256 colours |
| 286, VGA - Mode X, 256 colours, square pixels | 320 x 240 in 256 colours |
| 486, VGA - 16 colours in the window | A bigger window picture |
| 486, VGA - Mode X, 256 colours, square pixels | Mode X for a fast machine |
| Live on a CGA, Hercules or VGA desktop | A short clip, loaded whole, that plays in its window beside other programs |

## The preview

The right of the window shows **What the screen will show**: the encoded
frames as the target adapter draws them, in its own colours and at its own
pixel shape. Drag the slider under it to move through the video. The **<**
and **>** buttons beside the frame number go back or on one frame; so do
`,` and `.` once you have clicked the picture.

The **The file** box below says what the file holds. In it:

- **Title** and **Set title** change the name Video Player shows. Up to 47
  characters; the player shows the first 35. The video is not encoded
  again.
- **Redo posters** makes the file's desktop pictures again with the
  Desktop posters, size, clip and poster picture set on the form now: for
  a file made with the wrong ones, in seconds rather than a whole encode.
  A poster picture must be a picture file here, not a moment of the
  video.
- **Palette**, for a file whose colours were chosen from its clip, shows
  them. A file with a palette for each scene shows the one for the frame
  under the slider, and says which scene it is.
- **The poster** is the picture Video Player shows before a play.
- **Here** is the keyframe a play from the slider's position starts at.
  **Set as poster** makes it the poster, again without encoding again.

**Load .V88** opens a video made before, so you can look through it and
change its title or poster. When the file records the settings it was made
with, every field is set to them, so you can make it again with a change.
Dropping a `.V88` on the window does the same.

## Making a disk

With **...and make a disk of it** ticked, Encode also writes a disk image
beside the `.V88`, with the video on it under an 8.3 name (the first eight
letters and digits of its name, then `.V88`). Choose the disk in the list
beside the box:

| Disk | What it is |
|---|---|
| a 360 KB floppy, a 720 KB floppy, a 1.2 MB floppy, a 1.44 MB floppy | A floppy image with the video and Video Player. Put it in drive B: beside an os8088 system disk and double-click the video |
| a 20 MB hard disk: ST-225 on an ST11M (5150/XT) | A hard disk image that boots os8088 |
| a 32 MB hard disk: ST-238R on an ST11R (XT) | The same, for an XT's 32 MB disk |
| a 32 MB hard disk: IDE (286 and up) | The same, for a 286 or later with IDE |

A hard disk image is a fixed VHD for 86Box, or for a real disk written with
an imaging tool. It carries os8088, its hard disk and sound drivers, Video
Player and the video in its root. Where the encoder has no os8088 to put on
it, the hard disk is still made, formatted but not bootable: boot os8088 from
its system floppy, tick the hard-disk driver in the Control Panel, and the
disk appears as C:.

The list starts on the disk that suits the machine profile, until you pick
another.

## The tabs

Every setting the command line has is on a tab. Rest the pointer on a field
to see what it does. A field that takes one of a list has a **?** beside it:
click it for a window that explains every choice, with the current one in
bold; click a choice to take it. A group of settings that does not apply to
the file you are making is greyed out, and its tooltip says why.

| Tab | What is on it |
|---|---|
| Basic | **Made for**: the preset, the picture format, the machine profile and the aim. **The clip**: title, credits, start and end times, frames a second. **Loop**: where a repeating play loops from, and Repeat. **Held in memory**: resident and Live files. **Keyframes and the poster**: keyframe spacing, the poster, and the pictures stored for the desktop's Preview. **This computer**: how many processor cores the encoder uses. Any setting no other tab has is here too |
| Picture | The canvas (screen layout, size, how the picture fits), gamma, contrast and brightness, the grey stretch, the one-bit dither and invert, solid colours, and the text-mode settings |
| Colour | The palette choices for each colour format, with a **Palette** panel showing the colours as swatches. Hover a swatch to see its number and colour. **Scene join** takes times (in seconds, as the slider shows them, split by commas) at which a 16-colour video's colours should NOT change: the scene either side gets one set of colours chosen over both. Use it where the report says a scene change left the last scene up in the new colours - a flash of wrong colours for a few frames |
| Sound | The sound format, rate and volume, buffer sound ahead, ADPCM, and the PC speaker's shaping |
| Budget | The machine's disk rate, processor share and memory, an XMS bank for a 286 or better, and a layer for a faster disk |
| Trade-offs | What gives up some of the picture for a smaller file or less of the player's time: holding still areas steady, fewer pixels, tearing and second pages, and what is cut when a frame does not fit |
| Degrade to Fit | Tick **Use the degrade ladder** and choose its rungs - a lag, a slower frame rate, fewer colours, coarse cells for a whole frame or for only the parts that do not fit, or a cut that is small enough - tried in order where a frame would be cut. It is the ladder OR the settings in **Or, without the ladder: for the whole clip** (the lag, the slowest frame rate, coarse cells at a scene change and fewer colours a scene): with the ladder on, a setting there with no rung for it is off. **Held frames, either way** sets how a slower rate's held frames are priced and drawn. **By time**, under the rungs, gives a stretch of the video rungs of its own, in the command line's words: `7.3-7.8=lag:50,coarseany:frame,slow:2/3` uses those rungs for a stretch that starts between 7.3 and 7.8 seconds, and the ladder above everywhere else. Several are split by `;` |

## Using the command line

The shape of a command is the video, the file to write, and the settings:
`python3 tools/os88venc.py clip.mp4 CLIP.V88 --preset herc`

`python3 tools/os88venc.py --profiles` lists every profile and preset.
`python3 tools/os88venc.py --help` lists every option. The ones most people
use:

| Option | What it does |
|---|---|
| `--preset NAME` | The screen and size, from the presets below |
| `--profile NAME` | The machine it plays on, from the profiles below. The default is `5150-st225` |
| `--box WxH` | The largest size of the picture |
| `--fit fit`, `fill` or `stretch` | `fit` shows the whole picture with no bars; `fill` fills the box and crops the edges; `stretch` fills the box and distorts the picture |
| `--layout` | The screen memory layout, overriding the preset's: `cga`, `herc`, `lin80` (VGA's 640 x 480 mode) and the colour layouts |
| `--pixfmt` | The picture format: `mono` (black and white), `cgacomp`, `vga8`, `vga4`, `cga4`, `c160`, `c512` or `text` |
| `--start S`, `--end S` | Where in the video to start and stop, in seconds |
| `--fps F` | Frames a second. The default is the video's own, at most 30 (15 for 256 colours, 25 for the `vga8` and `modex` presets) |
| `--audio` | `pcm8` (8-bit sound for a Sound Blaster), `adpcm4` (half the bytes, noisier), `speaker` (the PC speaker) or `none`. The default is the profile's |
| `--rate HZ` | Sound samples a second. The default is the profile's |
| `--volume` | Louder or softer, for example `1.5` or `3dB` |
| `--dither` | For black and white: `bayer` (a steady 8 x 8 pattern, the smallest file), `bluenoise` (no visible grid, a little more data) or `threshold` (plain black or white) |
| `--stable N` | How close to the dither's threshold a pixel may be and keep its last value, so noise in the video does not flicker. 0 turns it off |
| `--gamma`, `--contrast`, `--brightness`, `--invert` | Adjust the picture; `--invert` swaps black and white |
| `--aim` | `asked` (the default), `quality` (spend spare budget on a bigger picture and better sound) or `size` (the smallest file for the look) |
| `--resident` | Read the whole file into memory before it plays |
| `--live cga`, `herc` or `vga` | A Live clip that plays in its window on that desktop |
| `--repeat`, `--loop-from S` | Start with Repeat on; loop back to this many seconds in |
| `--title T`, `--credits C` | The name Video Player shows, and a line for its information card |
| `--keysecs S` | Seconds between keyframes, the places a seek can start from. The default is 2 |
| `--poster K`, `--poster-at S` | Which keyframe is the poster, by number or by time |
| `--poster-image FILE` or `@S` | A picture of your own to show when the file opens, or the video's own frame S seconds in. A play still starts at a keyframe |
| `--desktop-posters` | A picture of each keyframe stored for the desktop's Preview, sharper than the one Video Player makes itself. `auto` (the default) stores them for 256 colours, CGA colours and 16-colour videos in colours of their own or too big for the window; `colour`, `mono` (black and white) or `both` for any video; `off` for none, which keeps the file smaller |
| `--desktop-clip N` | Like `--clip`, for those keyframe pictures: a colour within N levels of one the desktop has is drawn solid, so a background that is nearly black stays black instead of dotted. The default is 16; 0 dithers everything |
| `--desktop-size` | How big the Preview is on the desktop: `fit` (the default) as big as there is room for, or `half` or `quarter` of the video's size. A smaller Preview means Play goes full screen |
| `--posters-only` | Make an existing file's desktop posters again, with the four options above, and change nothing else: `os88venc --posters-only OLD.V88 NEW.V88 --desktop-posters colour` (the same name twice rewrites the file) |
| `--preview-png DIR` | Also save the screen once a second as PNG pictures in that folder |
| `--quiet` | Print less |

The presets:

| Preset | Screen and size |
|---|---|
| `cga`, `cga-small` | CGA black and white, 640 x 200 or 320 x 100 |
| `cga4`, `cga4-small` | CGA in 4 colours, 320 x 200 or 160 x 100, full screen |
| `c160` | CGA, 16 colours at 160 x 100, full screen |
| `c512` | CGA composite, about 450 colours, full screen on a real CGA |
| `herc`, `herc-mid`, `herc-full` | Hercules black and white, 400 x 200, 480 x 232 or the whole 720 x 348 |
| `herc-spk`, `cga-spk` | 360 x 160 at 23 frames a second with 8 kHz sound on the PC speaker |
| `vga`, `vga-mid`, `vga-full` | VGA black and white, 320 x 240, 400 x 300 or 640 x 480 |
| `vga4`, `vga4-mid`, `vga4-full` | VGA in 16 colours at the same three sizes. Plays in the window on a VGA desktop |
| `vga8`, `vga8-small` | VGA in 256 colours, 320 x 200 or 160 x 100, full screen |
| `modex`, `modex-small` | VGA Mode X in 256 colours with square pixels, 320 x 240 or 160 x 120 |
| `text`, `text-mono` | The 80 x 25 text screen, in colour or in black and white |
| `live-cga`, `live-herc`, `live-vga` | Small Live clips for each desktop |

The profiles:

| Profile | The machine |
|---|---|
| `5150-st225` | An IBM 5150 or XT with an ST-225 hard disk on an ST11M (the default) |
| `5150-xtide` | A 5150 or XT with an XT-IDE, or any disk the processor copies for |
| `5150-picomem2` | A 5150 with a PicoMEM 2 |
| `floppy` | Played off a 360 KB floppy: small and slow |
| `286-slow` | A slow 286 at 6 to 8 MHz |
| `286-fast` | A 16 MHz 286 with VGA on the board and an IDE disk |
| `286-speeddemon` | The fastest kind of 286, with fast RAM and a fast IDE disk |
| `286-picomem1` | A 16 MHz 286 with its disk on a PicoMEM 1 |
| `486-dx2-66` | A 486DX2/66 with an ISA VGA and IDE |
| `lossless` | No limits: every change kept, whatever it costs |

To save the sound alone for Audio Player, give a `.WAV` as the file to
write: `python3 tools/os88venc.py song.mp3 SONG.WAV`

## Putting a video on os8088

The easiest way is **...and make a disk of it** in the window. Otherwise,
copy the `.V88` onto an os8088 disk with your computer's own file tools:
os8088's floppies and hard disk images are ordinary FAT disks. Give the file
a short name, eight characters or fewer before `.V88`. On os8088,
double-click the file to open it in Video Player.

A video plays best from a hard disk. Make it with the profile of the
machine that will play it; a file made for a faster machine falls behind on
a slower one.

## Good to know

- The encoder never asks the machine for more than its profile allows. A
  busy scene that does not fit is drawn over a few frames instead, and the
  part that is still wrong is fixed in the frames after.
- Pick the profile for the disk the video will be read from. The same video
  made for `floppy` is much smaller and rougher than for a hard disk.
- **Live** clips are short: the whole file must fit in memory. With
  `--xms` (the **Held in memory** group) a Live file can be any length. It
  plays Live on a 286 or better that has the extended memory to hold it
  whole.
- PC speaker sound takes most of an 8088, so the speaker presets use a
  smaller picture and a lower frame rate.
- `--aim quality` makes trial encodes and takes longer.
- The encoder uses all your processor's cores unless you set fewer on the
  **Basic** tab (**This computer**) or with `--jobs`.
