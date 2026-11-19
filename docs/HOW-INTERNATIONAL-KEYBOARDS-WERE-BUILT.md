# How international keyboards were built

## Goal and constraints

Issue #385 asks for international keyboard mapping. The accepted delivery
plan is [INTERNATIONAL-KEYBOARD-PLAN.md](plans/INTERNATIONAL-KEYBOARD-PLAN.md).
The requested layouts are Spanish (Spain), Latin American Spanish and French
legacy AZERTY. The user requires an optional module and no net growth in
the core kernel; enabled module memory is reported separately.

## Contract foundation

The implementation branch starts at `b82e4e94310e`. Inspection found three
raw-input paths: the XT IRQ1 observer, the AT BIOS's INT 15h AH=4Fh handoff,
and kern_mega's native BIOS-ring producer. The physical-key map is a
separate facility and must remain physical. The text renderers and shared
fields currently stop at ASCII 126, so accented input requires a glyph and
text-acceptance change as well as layout tables.

SPEC.md §9.16 pins the optional module's ownership, character/control
boundary and no-growth gate before implementation. Module activation and
glyph use have separate holds: BIOS input may coexist with a document
that still needs accented glyphs.

The initial host setup is building the unmodified tree. No implementation
or emulator result has been claimed yet. Later entries record the actual
byte deltas, user-visible checks and screenshots as they become available.
