# How Mixer was built

## Foundation

The request combines a global desktop control with optional installed
software. The API index and Sticky Apps menu showed two reusable conventions:
quiet discovery each opening, and doing a package launch only after the
tracker releases the graphics lock. SOUND.DRV already owns the card's ports
and accepts package verbs, so the kernel needs no new mixer API cells.

The hardware reference is Creative's Sound Blaster Hardware Programming
Guide, chapter 4. A DSP is not evidence of a mixer: ordinary SB 1.x/2.0 and
AdLib hardware need no speaker affordance. The three mixer generations have
different channel masks and bit depths; the driver exposes percentages and
reads back the real quantized setting.

The visual brief is an engraved scale and a ridged mechanical thumb, drawn
from shared source for the popup and package. The face must be designed in
black and white first so Hercules and CGA are first-class presentations.

Implementation and measured acceptance results will be added with each wave.
