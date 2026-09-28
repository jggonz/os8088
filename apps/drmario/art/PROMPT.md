# DrMarco screen artwork

Generated with the built-in imagegen tool on 2026-09-28. The original output
is `drmarco-screen.png`. Native VGA/CGA caches are derived at build time.
This replaces the screen surround only; capsule and bottle-virus tiles still
come from the user-supplied NES reference.

Gameplay animation (2026-09-28) reuses this source unchanged. The offline
compiler makes open/closed eye patches for DrMarco and draws three
original geometric germ mascots with waving arms, googly eyes and dizzy faces.
These procedural poses live in `tools/drmario_assets.py`; their palette-native
streams and pose preview PNGs are generated under `build/drmario-art/`.

## Generation prompt

Use case: stylized-concept
Asset type: production background artwork for a native 320x240 pixel-art falling-capsule puzzle game called DrMarco.
Primary request: Create an original retro medical laboratory game screen surround with an original friendly doctor character. This is a flat game background, not a device mockup. Landscape aspect ratio 4:3. Design on a coarse 320 by 240 logical pixel grid, with crisp large pixel clusters, hard edges and no gradients or anti-aliasing.
Composition: The playable bottle interior occupies exactly logical x=96..223, y=32..223, and must be completely solid black with no pills, viruses, lines, texture or writing. Around that rectangle draw a narrow glass laboratory bottle frame in blue, dark blue and white, with shoulders and a small neck at the top. Keep left HUD area x=0..79, y=24..199 entirely solid black and right top HUD area x=240..319, y=24..75 entirely solid black. Keep the full top strip y=0..15 and bottom strip y=228..239 solid black for game-drawn lettering. On the right only, inside x=242..311, y=80..164, draw a cheerful original scientist doctor with round spectacles, dark swept hair, clean-shaven face, white laboratory coat and stethoscope, holding a red/yellow capsule and facing toward the bottle. Make this character distinct from Nintendo Mario: no mustache, no red hat, no Nintendo characters. Below the doctor, a tiny laboratory flask and a pair of playful geometric germs can fill x=244..310, y=166..184. The left side stays empty for the score panel. Add restrained pixel highlights on the bottle sides and shoulders without crossing into its black interior.
Palette: flat black, white, bright cobalt blue, red, yellow, dark navy, dark red and ochre only. Most of the image is pure black negative space. The art must survive downsampling to 320x240 and four-color CGA.
Text: none. No labels, no letters, no logo, no watermarks. The engine draws the exact DrMarco title itself.
