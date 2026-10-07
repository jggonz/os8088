# DrMarco splash artwork

Generated with the built-in imagegen tool on 2026-09-28. The selected source
is `drmarco-splash.png`. The user's original Dr. Mario title-screen screenshot
was the composition reference; `drmarco-screen.png` was the character reference.
The reference screenshot remains user-supplied; it is not bundled with the game.

`tools/drmario_assets.py` converts the source to native EGA/VGA colors and
palette-boundary line art, reserves runtime text surfaces, and assembles help
from the existing gameplay portrait and capsule/virus tiles. The compiler emits
`DRMARCO.VGA`, `DRMARCO.CGA`, `DRMARCO.HRC` and uncompressed previews in the build
folder. The native font supplies the menu/help/settings text.

## Final prompt

Use case: compositing. Asset type: final retro pixel-art title-screen background for the native game DrMarco. Input image 1 is the user's explicit COMPOSITION reference: a huge horizontal capsule logo across the upper half, checkerboard backdrop, and wide rounded black menu plaque in the lower half. Input image 2 is the actual DrMarco game's CHARACTER reference: use this same blue-haired scientist with large round glasses, white coat, stethoscope, clean-shaven face and red/yellow capsule; preserve his recognizable sprite identity. Create a new DrMarco title screen matching image 1's strong simple composition and real game-sprite pixel art. Landscape 5:3, intended native resolution 432x264. Across x16..416,y16..116, a large glossy but strictly flat-color pixel-art capsule: left half cobalt blue, right half red/magenta, white pixel highlight, black pixel outline/shadow. Inside it put LARGE bold yellow pixel-art letters exactly 'DrMarco' (D-r-M-a-r-c-o), clearly legible and carefully centered, filling most of the capsule. Background: crisp 12-pixel green and dark-green checkerboard like reference 1. Lower half: a broad rounded BLACK menu plaque, approximately x20..412,y144..248. Inside its LEFT end x28..85 place a small detailed DrMarco sprite from reference 2, and inside its RIGHT end x354..402 place three small colorful playful virus/capsule sprites. Keep the CENTRAL plaque area x92..346,y152..240 completely PURE BLACK and EMPTY for runtime ENTER/H choices and settings; do not place any artwork or text in that area. All text except the single DrMarco logo must be absent. No Nintendo wording, no copyright line, no trademark, no menu lettering, no slogans, no fake controls, no device/window frame. Use authentic coarse sprite pixels and sharp clusters sized for the 432x264 display, not smooth vector shapes. Flat classic EGA/VGA palette, no gradients, no antialiasing. The result should look like a complete 1990 puzzle-game title screen using the existing DrMarco art, not a software dialog or a modern card layout.
