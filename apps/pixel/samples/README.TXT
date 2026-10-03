PiXEL's sample gallery
======================

Nine pictures for PiXEL, the os8088 image viewer (SPEC.md 106), one in each
format and shape its decoders read, so every one of them has something to
show:

  VACATION.JPG  640x480  baseline JPEG, quality 85, 4:2:0
  FLOWER.JPG    640x480  baseline JPEG, quality 80, 4:4:4
  ROOM.JPG      640x480  progressive JPEG, quality 80
  LAKE.JPG      320x240  greyscale JPEG
  CAT.GIF       320x240  256-colour GIF
  BALLOONS.PNG  320x240  8-bit (palette) PNG
  HOUSE.PNG     320x240  truecolour PNG
  MOUNTAIN.BMP  256x192  24-bit BMP
  CITY.PCX      320x240  8-bit PCX

Where they come from
--------------------

All nine are ORIGINAL images, made for this project: nine 1200x896 scenes
generated for os8088 and not taken from anywhere else. Nothing here is a
photograph of a real place or a copy of someone else's work.

tools/pixsamples.py reduced each scene to the size above (a centre crop to
the target's aspect, then Lanczos) and re-encoded it with Pillow into the
format listed. The large originals are not kept in the tree; the files in
this folder ARE the artefacts, and `python3 tools/pixsamples.py --check`
(run by every `make`) pins each one by its SHA-256, so none of them can change
without the commit saying so.

They are released under the same terms as the rest of os8088.
