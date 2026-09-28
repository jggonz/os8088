# MartyPC: defects found through os8088, for a patch someday

> **OPEN, AND NOTHING IN IT IS STARTED.** Each entry is a place where MartyPC
> (the pinned build in `tools/martypc/`, docs/MARTYPC-DEBUG.md) does
> something a real machine does not. They were found while testing os8088
> and recorded rather than worked around. The owner's word on the first
> (2026-09-27): *"we'll get around to patching it sometime."* A fix lands as
> a patch in `tools/martypc/patches/`. That directory's README says how to
> cut one against the right base, and that is the trap it has already
> sprung twice.

**Why this file exists.** A gate that asserts on an emulator defect is a
gate about the emulator. So the rows here were written round each one, and
the knowledge sat in comments where nobody deciding what to patch would look.
Each entry has four parts:
- what a real machine does;
- what MartyPC does;
- where in MartyPC's source it comes from, as far as it has been traced;
- what in os8088 is shaped round it, so that a fix can be followed by taking
  the workaround out.

## 1. The VGA draws text colour 6 red, not brown

**Symptom.** In a VGA text mode, attribute colour 6 is drawn as (light)
red. A real VGA, and MartyPC's own CGA, draw it brown.

**Where it has been met:**
- **The Telnet client's full-screen console** (SPEC.md 70.8.9): a cell of
  `0x64`, red on brown, is red on red there and its glyphs vanish. The
  windowed renderer, mode 12h through the DAC, draws the same cell
  correctly. Both were photographed side by side.
- **The Video Player's C160 mode** (SPEC.md 98.3.12): 160 x 100 in sixteen
  colours on the text-mode hack plays through a VGA's text mode, so every
  brown pixel is red under MartyPC.

**What is shaped round it:**
- `tests/telpen.py` uses no colour 6.
- The C160 rows compare the text buffer's ATTRIBUTES, never the rendered
  colour.
- VIDEO-PLAN 15.7 carries a warning not to "fix" C160's palette for it.

**Where it comes from - traced, the fix NOT made or tested.** In
`crates/marty_core/src/devices/vga/attribute_controller.rs` (MartyPC
0.4.2, the pinned `e15cb04`), `apply_attribute` and `apply_attribute_9col`
choose a text cell's colours like this:

```rust
match clock_select {
    ClockSelect::Clock25 => {
        fg_color = self.palette_registers[fg_index].four_to_six as usize;
        ...
    }
    _ => { fg_color = self.palette_registers[fg_index].six as usize; ... }
}
```

`AttributePaletteEntry::set` makes `four` out of a palette register's bits
0-3 with bit 4 folded into bit 3, and `four_to_six` is
`CGA_TO_EGA_U8[four]`. The BIOS loads palette register 6 with **0x14**, the
EGA/VGA brown. Folded, that is `0x04 | 0x08` = **0x0C**, which the CGA table
maps to light red.

Folding the register to four bits is what an EGA does in its 200-line modes,
whose monitor takes only four colour lines. A VGA does not: the attribute
controller's six bits (with the colour-select bits) always index the DAC.
**The likely patch is to take `.six` on every clock** for the VGA's
attribute controller, and leave the EGA's own copy of this code alone.

**The gate for the patch.** Boot `os8088_xt_vga` and play a C160 clip. Then
read a brown pixel off `fbuf`: it must be the DAC's entry 0x14 (2A,15,00),
not red. A real VGA and 86Box draw brown already.

## 2. A key press lost now and then under parallel load

**Symptom.** A key sent with `Marty.key` never reaches the guest: no
keyboard interrupt, no make and no break. It happens about one run in five
when four emulators run at once, and when the guest is busy (the Mode X page
flipper). It has not been seen with one instance on an idle box.

**What is shaped round it.** `tests/vidfskeys.py`'s `press()` sends a key
again, up to three times, when what it does never happens. It PRINTS how
many it resent, so the count cannot hide. `vidpreview`'s lost Esc at a
bracket's teardown (VIDEO-PLAN 15.7) may be the same thing or may be the
guest's. The owner's word on that one: not a concern for a person, who is
waiting for the full screen to close.

**Where it comes from - NOT KNOWN.** It is either the harness, meaning
`Marty.key`'s make/break timing against a guest that is not polling, or
MartyPC's keyboard: its PPI and 8255 shift register, and what happens to a
scancode that arrives while one is still unread. VIDEO-PLAN 15.7 puts the
diagnosis in **the next full soak pass**, which is the owner's call to run
(CLAUDE.md, Testing). Counting keyboard interrupts against keys sent,
across a soak, answers which side drops it.

## 3. The fixed disk is XT-IDE only, and takes no 15-head geometry

**Symptom.** MartyPC's `[machine.hdc]` is `XtIde` with `format = "Mfm"`. A
VHD at 615/4/26 (its bundled `default_xtide.vhd`) boots. The two layouts
os8088's own hard disks are cut for do not:
- **the Seagate ST11's layout** (an ST11M's ST-225 at 615/4/17, an ST11R's
  ST-238R at 615/4/26): the card hides cylinder 0 behind its parameter
  record, so XT-IDE reads that record where the MBR should be;
- **a 286's IDE disk at 250/15/17**: the machine never reaches the desktop.
  Where it stops has not been traced. The same image at 615/4/26 boots and
  plays.

**What is shaped round it.** `tests/vidhdmake.py` boots the encoder
window's hard disk (SPEC.md 98.2.12.1) with only the geometry put to
615/4/26. The ST11 layouts and the 15-head disk are checked for structure by
`os88disk.py --verify-hdd` (`tests/vencguitest.py`) and booted on 86Box and
the owner's 5150, never here.

**What a patch would be.** An ST11 controller is a device of its own:
- its option ROM, and its DMA transfer;
- the ROM this tree cannot ship.

It is a wish rather than a defect. The 15-head refusal may be a defect
(XT-IDE's BIOS supports any CHS), and would be the first thing to trace.
