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
53 passes, no failures and four optional C/PageDesk skips. That was the pre-implementation baseline; the combined QA below follows it.

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
The integrated big kernel initially added 343 exact permanent bytes. The
solution is a measured split of directory-mutation bodies into the existing
disk-operations module (§18.5.3), plus a smaller control thunk. The module
loader's own read/write/commit dependencies stay resident.

The first combined QA passed independent assembled-code layout vectors,
the optional glyph provider and stable legacy ASCII pointer checks, active
queued 8042 input, all five Control Panel picks, KL save/reboot, malformed
records, clean sample release and missing-driver refusal. These are emulator
observations; no physical Spanish or French keyboard has been tested.

The native text pass exercised physical keys for both Spanish variants and
French, accented Find/Replace, two glyph-owning windows and BIOS-default
repainting. Its test harness needed explicit guest budgets, an unambiguous
Find menu item and an actual save-completion witness. It also exposed that
the older source-based Note Pad state reader counted fields in an inactive
benchmark block; the new row reads NASM equates only after proving the
symbol assembly byte-identical to the shipped raw package. Fresh VGA/CGA/Hercules captures now show the actual accented text and complete Keyboard page. The final regression pass is recorded below.

## Lifetime failures found on the machine

With Spanish active, hibernate resumed into a damaged module: the original
font repair copied 760 bytes to the heap font's base, which was now the
module header. The before/after header read made the cause concrete. The
provider now suspends around original-font repair, then refreshes its ASCII
copy and rearms with layout and client bits preserved. Both explicit input
and BIOS-default glyph-only documents resume on the XT and under Bochs.

Mega's alias setup also overwrote the instance stamp in DH. Typing worked,
but closing Control Panel discarded both documents' font ownership. Saving
DX around alias derivation fixed the actual two-instance scenario. A separate
high-memory package enters a real-mode fullscreen bracket and proves that
legacy glyph borrowing keeps ASCII bounds there, then restores CP850 and
physical Spanish input after returning to protected mode.

The missing-CLI, damaged glyph, missing hibernate suspend, missing alias
save and missing real-mode clamp controls all fail their intended assertions.
Guest corruption arms also detect duplicate AT words, the wrong saved
layout, a leaked document hold and leaked failed-entry ownership. The DOS
owner flag control restores French a/q/z/w where BIOS q/a/w/z is required,
so the compatibility test cannot pass with a globally untranslated keyboard.

## Memory and the system disk

The [complete A/B report](reports/INTERNATIONAL-KEYBOARD-SIZE.md) uses NASM
3.02 and build number 343 for all four kernels. Every permanent/core/shipped/
resident total stays at or below baseline. The stripped cores shrink by
176 bytes on big/emu, 128 on small and 16 on mega. Enabled keyboard memory
is one optional 5.5 KiB claim, or 6 KiB on mega; the last client frees it.
The small kernel retains its existing 70.5 KiB heap floor on a 128 KiB PC.

Those savings have a user-visible cost: Rename, New Folder and folder
removal now load the existing DISKOPS module from the system disk before
mutation. Shared FAT commit/rollback and module-loading dependencies remain
resident. Twelve actual API scenarios check success and absent/mismatched
modules, registers, B: mount/CWD restoration, write fencing and an independent
FAT check; refusal leaves both images byte-identical. Compression's nested
module hold is retained until its original owner releases it.

## XT cost and visible evidence

The full KL_SCAN call/return is 123 cycles with the already-armed observer
and no module. Tested explicit paths range from 1,595 to 6,936 cycles,
including an unsupported dead-key pair that searches the whole bounded
composition table. At 4.77 MHz that maximum is about 1.45 ms. Actual module
execution uses at most 26 stack bytes. No allocation, file I/O or kernel API
is reachable from the IRQ translator.

The native text check physically swaps the system disk out after activating
French, types and repaints an accented character, and then restores the
system disk. Input needs only the pinned provider. Saving a preference with
a foreign disk in A: reports the existing failure toast and keeps the active
French layout and retry flag; it does not write to the foreign disk.

The [plan's QA table](plans/INTERNATIONAL-KEYBOARD-PLAN.md#combined-qa-observations-and-fixes)
records the defects and repairs. Screenshots and final suite results are
attached to the feature PR. Physical international keyboard testing and a
reference Windows program run are not claimed.
