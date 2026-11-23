# Original-grid portrait source and runtime

`provided-animation-grid.png` and `provided-lesson-scene.png` are the exact
user-supplied images. `ANIMATION-SOURCE.json` records their hashes, grid/crop
coordinates, palette and selection rules. The older generated master/prompt
are historical; the active portrait source is the supplied grid.

`tools/os88btanim.py draft GRID SCENE` is a one-time Pillow authoring operation.
It crops all thirty source cells without their grid labels/borders. Idle/blink
uses the neutral body with the source eye patch, and talk uses the source mouth
patch, so small motion does not make the camera/jacket jump. Praise, roast and
facepalm preserve the whole original gesture. The scene is the unobstructed
sky/mountain/yurt strip from the supplied lesson, behind the original navy mask.
The committed animation.txt is authoritative indexed source; sprites.txt and
blink.txt select matching static/mono/CGA poses. The explicit mono threshold
and EGA mapping are deterministic. No generative service is used.

`build` reads only text and existing standard-library packers. Every frame has five strip signatures for the 96 × 120 color pose,
two for 40 × 48 color, and two for 40 × 48 monochrome.

| Package part | Contents |
| --- | --- |
| 0, 1, 2 | Static art and type, sound and display lettering, eager code overlay |
| 3 | Directory and compressed 40 × 48 color/mono strips |
| 4 | Native four-plane frames 0–9 |
| 5 | Four window illustrations and ten compact achievement badges |
| 6, 7 | Native four-plane frames 10–19 and 20–29 |

A color window opens with the small shapes and menu illustrations. Starting a
lesson loads the three large pose banks before the scoring clock starts.
Pause keeps the poses. Returning to a menu frees them, and resuming from
that menu can load them again. If the raw poses will not fit, the smaller
animation continues from part 3. A monochrome window needs only part 3.

Each motion bank starts with a 16-byte BTAN header containing its exact length.
Part 3 contains 270 eight-byte directory records. A record holds the part
number, source offset, compressed byte count (zero for a raw strip) and
content signature as little-endian words. A raw frame is 5,760 bytes. Each
of its four planes has 120 rows of 12 bytes; the plane step stays 1,440
bytes when drawing only the eyes. The native VGA drawing call takes its
pixels directly from the movable art part and obeys the window clip.
Displays that decline planar data can copy individual 24-row strips into
the shared band. Compact and monochrome frames use bounded OS LZ4 decoding.

The clock latches a reaction frame until its visible update has finished.
Blink, caption, music-meter and score updates run through independent state.
The packaged-art decoder and host geometry checks cover bounds, relocation
and compact fallback. The installed XT motion check matches all 11,520
displayed pixels of each of the six roast poses with the committed artwork.
