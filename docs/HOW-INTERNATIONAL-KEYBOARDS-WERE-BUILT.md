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

The initial host setup and normal build passed, followed by the small,
emu and mega builds. The [size evidence](reports/INTERNATIONAL-KEYBOARD-SIZE.md)
records all four baselines before implementation. The normal fast tier had
53 passes, no failures and four optional C/PageDesk skips. Emulator checks
of the keyboard feature remain pending.

## Compatibility boundaries

The module's unloaded dispatch already returns carry set. Making carry set
mean pass-through lets the raw hook use that existing refusal without an
extra resident active flag. Module activation arms the existing physical-key
observer, rather than adding another port-read predicate.

WIN16 changes its instance name to the guest program's name. A name list
would miss those windows, so the contract uses a flag in the existing
instance record. DOS/WIN16 can set it without loading the keyboard module;
the optional raw handler reads it when choosing the foreground input policy.

Existing packages cache ASCII font pointers and sometimes have fixed glyph
buffers. Extended-text support must preserve those clients' original bounds
and pointer lifetime. The optional provider and new prose fields acquire
explicit ownership instead of broadening old callers' buffers implicitly.
