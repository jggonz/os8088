# Borat portrait sources

`master.png` and `PROMPT.md` retain the original generated source and exact
prompt. `sprites.txt` is the selected exact-size, explicit palette source
used by every build; regenerate intentionally with `tools/os88btart.py draft`.
See `docs/boratype/face.md` for palettes, fonts, records and commands.

The seven expression states are welcome/attentive/pleased/smug/disappointed/
laughing/shocked. Cell8 (attentive blink) is selected into blink.txt, with four variants in
sound part1 at IDs258..261. Art part0 remains unchanged.
Color13 is reserved by the shared renderer as transparency and never occurs
in these opaque portrait sources. No PNG decoder or image API runs at build
or in the guest. Sound score sources in this directory belong to the audio
lane and are independent of these records.

The gold masthead is a 432 × 24 binary mask cropped from the owner's VGA
lesson reference. Normal builds pack it into art bank 1 as consecutive
16-pixel font strips. The lesson, key and label strikes are adapted from
the repository's PageDesk Liberation Serif Bold bitmaps. Recreate them with
`python3 tools/os88btletters.py`. To reselect the title, pass
`--reference` and the original 1448 × 1086 lesson PNG.
See [Liberation's license](../../pagedesk/fonts/OFL-Liberation.txt).

The five 24 × 24 achievement badges come from the provided Records screen.
Select them again with `tools/os88btletters.py --records <records.png>`.

The uppercase letters, numbers and semicolon in the 16-pixel strike use the
selected VGA keyboard legends in `key-legend.mask`. Lowercase, punctuation
and the other two strikes retain their Liberation sources.

The lesson, trophy, result and listening illustrations are four opaque
four-plane images sized for their panels. Lesson selection and Settings keep
the reference speech balloons inside the image.
The file names, crop coordinates and sampling rule live in
`tools/os88btscenes.py`. Re-select with `--select <reference-directory>`.
The final animation bank carries their four 16-bit record offsets immediately
after its 16-byte header. The renderer copies the visible bytes of each plane into the window paint
band. The normal window compositor handles occlusion and display output.
