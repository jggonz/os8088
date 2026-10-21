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

## Hardware wave

SOUND.DRV now owns descriptors for the three mixer families and offers
INFO/GET/SET through its existing package door. Detection checks two values
and restores the tested register. Source percentages round to the actual
3/4/8/16/32-level hardware, stereo transactions are atomic, and the driver
saves only exposed registers for detach. Mixer availability is independent
of PCM-buffer ownership and the selected sound tier.

Host verification: `make small` and `make` passed all 55 fast checks after
building the small system disks required by the install-manifest check.
Hardware and interaction evidence belongs to the final `mixer` row.

## Quick-volume wave

The first CGA boot showed the intended compact mechanical slider. It also
exposed two defects worth looking for in assembly UI work: loading a color
into AL after calculating AX changed a rectangle's x coordinate, and file
discovery left the busy/activity affordance up for the life of the popup.
The drawing paths now set colors before loading geometry, and the kernel's
small modal bridge finishes file activity and owns cursor/event bookkeeping.

The slider is composed with os88gfx.inc and emitted as one opaque band.
Only the control band changes during a drag; its baseline stereo ratio
survives silence and return. Save-under is sized for the actual adapter and
optional command: 2/3KB on mono, 8/10KB on color. It is freed at dismissal.

The driver exceeded the original HMA address window. The C000 origin gives
16KB of offsets, keeps owner aliases below FB00, and leaves the 24KB pool
unchanged. The host build checks relocation against a second assembly.
Affected final rows include mixerhost, mixer adapter/UI rows, hmadrv,
sndmove, stikmenu and toastbar.

## Optional package wave

The native Mixer now builds and opens from the discovered command. The first
VGA inspection showed the shared fader, a channel list, clear mono balance
status and the system About convention. The source list comes from the
driver; unsupported entries use the standard disabled pen. Bass and treble
use left/right cut/boost controls instead of a volume balance ratio.

MIXER.O88 joins SYSTEM on the larger system floppies and live media; it is
omitted from the 360KB system disk and every small-kernel disk. A large-disk
installation copies it as an ordinary file. Discovery is repeated at each
opening and at launch. The popup's Open Mixer row now has a separator and
an inverted hover treatment.

The initial idea borrowed three cells from the clock. Review of the toast
contract rejected that: its maximum message needs the existing field. The
right-hand boundary now moves left by three cells while the speaker is
present, preserving the entire clock and toast field. Application menu
layout keeps its existing prefix rule.

Host builds have passed the 55 fast checks. The final acceptance pass is
being prepared: three adapter sessions, SB16 in heap and HMA, independent
mixer register models, driver movement, Sticky Apps and toast regression.
