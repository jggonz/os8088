# WireBall splash artwork

Generated using the built-in imagegen tool. `splash-source.png` is the original
selected output, copied into this repository. `tools/wbfront.py` crops the
black margins at (0,96)-(2079,616), scales to 416x104, and emits the native
EGA 4bpp and Bayer-dithered 1bpp artwork in `wbfront.inc`. Compact windows use
alternate source rows. No image-generation service is needed to rebuild.

`splash-preview.png` is a VGA emulator capture of the native splash, produced
by `python3 tests/wireball.py --front`; it is review evidence, not a build input.

Final prompt:

> Use case: stylized-concept. Asset type: original illustration for the splash screen of WireBall, an actual Intel 8088 Pong game that connects two computers over a serial null modem. Create a very wide, horizontal retro pixel-art title banner, approximately 4:1 aspect ratio, framed with generous dark margins to allow cropping to a 416 by 104 pixel strip. Pure black background. Exact prominent readable title text: "WIREBALL" in large bold angular arcade pixel lettering, centered in the upper half, warm off-white with a subtle cyan accent. In the lower half, two charming beige 1980s CRT monitors face the viewer, one on the left and one on the right, connected by a visible bright cyan serial cable with simple recognizable DB9 plugs curving between them. Each monitor shows one bright vertical Pong paddle at its outer edge, and a tiny glowing warm-yellow square ball with a short motion trail travels from one screen through the cable toward the other. Strong clean silhouettes, blocky limited-palette sprite aesthetics, sparse selective pixel dithering, cyan and off-white highlights and a small golden ball on black. Delightful polished vintage computer game box-art feeling, not photorealistic. Keep every major element bold enough to stay readable when reduced to 416 pixels wide and when converted to monochrome. No small text, no instructions, no extra words, no branding other than WIREBALL, no gradients, no modern laptops, no cables leaving the frame. This is only the hero artwork; real buttons and instructions will be rendered by the operating system below it.
