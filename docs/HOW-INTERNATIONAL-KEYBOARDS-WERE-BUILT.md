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


## Combined implementation and first QA

The module, glyph provider and settings were integrated as a dependency
group. The first combined build assembled the normal image and packages,
then correctly stopped at the small kernel's unchanged heap-floor guard.
The integrated big kernel initially added343 exact permanent bytes. The
solution is a measured split of directory-mutation bodies into the existing
disk-operations module (§18.5.3), plus a smaller control thunk. The module
loader's own read/write/commit dependencies stay resident.

The first combined QA passed independent assembled-code layout vectors,
the optional glyph provider and stable legacy ASCII pointer checks, active
queued8042 input, all five Control Panel picks, KL save/reboot, malformed
records, clean sample release and missing-driver refusal. These are emulator
observations; no physical Spanish or French keyboard has been tested.

The native text pass exercised physical keys for both Spanish variants and
French, accented Find/Replace, two glyph-owning windows and BIOS-default
repainting. Its test harness needed explicit guest budgets, an unambiguous
Find menu item and an actual save-completion witness. It also exposed that
the older source-based Note Pad state reader counted fields in an inactive
benchmark block; the new row reads NASM equates only after proving the
symbol assembly byte-identical to the shipped raw package. Final evidence
and the complete verification pass remain pending.
