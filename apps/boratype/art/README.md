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
