# Joystick page mockups (SPEC.md 136.6)

The design reference for the Joystick Control Panel page, generated on
2026-11-23 with Codex CLI 0.170.0's built-in image generation from
`MOCKUP-PROMPT.md` (verbatim). Five screenshots of the running OS were
attached as references - the wide face's Drivers page at VGA 640x480 and VESA
800x600, the compact face's Drivers page on EGA and CGA, and the wide face's
original mockup (`docs/cpwide/`):

    codex exec --skip-git-repo-check --sandbox workspace-write -C <dir> \
        -i vga-640x480-drivers.png -i vesa-800x600-drivers.png \
        -i ega-compact-drivers.png -i cga-compact-drivers.png \
        -i cp-drivers-vga-640x480.png < MOCKUP-PROMPT.md

Every pixel is one of the 16 EGA/VGA colours (the CGA mockup black and white
only; the icons' background the #FF00FF transparent key).

| file | what |
|---|---|
| `cp-joystick-vga-640x480.png` | the wide face, VGA 640x480: stick A live, stick B not connected |
| `cp-joystick-calibrate-vga-640x480.png` | the wide face during calibration, step 2 of 3 |
| `cp-joystick-vesa-800x600.png` | the wide face, VESA 800x600, flight-stick type: throttle, rudder, hat |
| `cp-joystick-ega-compact.png` | the compact face, EGA 640x350 |
| `cp-joystick-cga-compact.png` | the compact face, CGA 640x200 |
| `joy-icon-16.png`, `joy-icon-32.png` | the page-list and header-strip icons (copied into `kernel/cpwart/`'s sheets) |

They are a design, not a screenshot: where the built page differs, SPEC.md
136.6 says why.
