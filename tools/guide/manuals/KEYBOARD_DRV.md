# Keyboard layouts

`KEYBOARD.DRV` supplies US QWERTY, Spanish (Spain), Latin American Spanish
and legacy French AZERTY input, together with accented CP850 glyphs for
native text. Keep the file from the same build and kernel as the system disk.

## Choosing a layout

Open [[CTRL.DRV]] and click **Keyboard**. Select a layout and type in the
sample to check the keys. Backspace edits it; Escape clears it. **BIOS
default** returns to the machine's original input behavior.

Close Control Panel to save the choice for the next start. On a small
system it lasts until restart. A missing or mismatched module leaves the
working layout in place; an invalid saved choice starts with BIOS input.

## Accents

Press a dead accent, then a letter to compose it. Accent followed by Space,
or the same accent twice, types the accent. An unsupported pair types the
accent and the letter. Backspace cancels a pending accent; changing layout
or focus cancels it too.

Use right Alt for AltGr on an enhanced keyboard, or Ctrl plus left Alt
on an XT keyboard. A layout cannot add keys the hardware does not have.

[[NOTEPAD.O88]] accepts, edits, finds, replaces, saves and reopens accented
CP850 text. Existing accented text stays readable after choosing BIOS input.
DOS and WIN16 retain their existing BIOS input and guest character sets.
Unicode, the Euro sign and modern French variants are not supported.

## Memory and disks

BIOS input with no accented native text uses no keyboard module memory.
Selecting an explicit layout loads one 5.5 KiB claim, or 6 KiB on mega.
Typing and drawing then work with the system disk removed.

An accented note, Find field or Control Panel sample keeps the glyphs
available even with BIOS input selected. Clear those fields or close the
last client to release the module. Opening an accented file may load the
glyph provider while BIOS input remains selected.
