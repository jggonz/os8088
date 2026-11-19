# Keyboard layouts

Open **Apple → Control Panel → Keyboard** and select a layout. The change
applies immediately; the sample below the choices lets you check the keys.
Backspace edits the sample, and Escape clears it.

| Choice | Input |
| --- | --- |
| BIOS default | The machine's existing BIOS translation |
| US QWERTY | Explicit US layout |
| Spanish (Spain) | Spain's Spanish layout, including ñ and dead accents |
| Latin American Spanish | Latin American punctuation and AltGr positions |
| French AZERTY | The legacy French AZERTY layout |

Close Control Panel to save the selection. The big, emu and mega kernels
restore it at the next boot. On the small kernel the choice lasts until
restart; its panel says so.

## Optional memory

BIOS default does not load `KEYBOARD.DRV`. Selecting an explicit layout loads
that file from the system disk into an optional heap claim: 5.5 KiB on
big/small/emu, or 6 KiB on mega. Once loaded,
typing does not require the disk to remain inserted. Keep the driver from
the same build and kernel variant as your system disk.

Switching to BIOS default releases the translator when no native text client
still needs its accented glyphs. An accented Note Pad document, find field,
or Control Panel sample can keep the glyph provider loaded. Clearing those
fields or closing the last client releases it. Reading an accented file can
load the glyph provider while input stays at BIOS default.

If the driver cannot be loaded, selection leaves the current working layout
in place. A missing or invalid saved selection falls back to BIOS default.
Note Pad preserves accented file bytes if the glyph provider is unavailable;
it reports that the keyboard module is needed.

## Accents and older keyboards

Native text uses **CP850**, with one byte per character. Note Pad accepts,
edits, finds, replaces, saves and reopens those bytes. This includes Spanish
`ñ Ñ á é í ó ú ü ¿ ¡` and French `é è à ç ù ê ë`.

Press a dead accent and then a letter to compose it. Accent followed by Space,
or the same accent twice, inserts the literal accent. An unsupported pair
inserts the accent and letter. Backspace cancels a pending accent. Changing
the layout or focus also cancels it.

On an enhanced keyboard, use right Alt for AltGr. On an XT keyboard, Ctrl
plus left Alt provides the same modifier. Keys the hardware lacks, including
the extra ISO key, cannot be created by a layout. Plain left Alt continues
to serve BIOS shortcuts.

These are native desktop layouts. DOS and WIN16 keep their existing BIOS
and guest character-set behavior. Physical key-state controls used by games
retain their physical scan positions. Filenames remain under the existing
filesystem rules. Unicode, the Euro sign, and modern French AZERTY variants
are outside this implementation.

## Adding a layout

The contract is [SPEC.md §9.16](../SPEC.md#916-optional-international-keyboard-module-keyboarddrv).
Character definitions live in `tools/keyboardmaps.py`; the generated
`kernel/keymaps.inc` is part of `KEYBOARD.DRV`, outside the permanent kernel.
Use an authoritative layout reference for all modifier columns and dead
keys, then add independently authored physical scan vectors in
`tests/kbdlayouts.py`. Do not generate expected test values from the layout
implementation. Update the stable ID contract, selection and settings
validation together, and regenerate the tables with
`python3 tools/keyboardmaps.py`.

Folder creation, renaming and folder removal also use the existing optional
disk-operations module. Keep the system disk available when first using
those commands.

The [delivery plan](plans/INTERNATIONAL-KEYBOARD-PLAN.md) and
[build diary](HOW-INTERNATIONAL-KEYBOARDS-WERE-BUILT.md) record the architecture,
measured memory costs and validation evidence.
