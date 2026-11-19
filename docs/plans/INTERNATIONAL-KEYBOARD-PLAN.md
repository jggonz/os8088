# International keyboard mapping delivery plan

Issue: [#385 — International Keyboard Mapping](https://github.com/jggonz/os8088/issues/385).
Baseline inspected: `b82e4e94310e` (2026-11-19).
Status: implementation started; stories remain unchecked until verified.
Build diary: [How international keyboards were built](../HOW-INTERNATIONAL-KEYBOARDS-WERE-BUILT.md).

## Outcome and scope

A user with a **Spanish (Spain)**, **Latin American Spanish** or **French
legacy AZERTY** keyboard can select that layout in Control Panel and type
the expected letters, numbers, punctuation and supported accented characters
in native text fields and Notepad. An optional keyboard module supplies these
three layouts and an explicit US QWERTY map. BIOS default preserves today's
input behavior with the module unloaded. Additional layouts become data
additions to the module.

Each layout's modifier and dead-key behavior is pinned to a named reference.
Belgian, Canadian, BÉPO and the newer French standard layout are separate
layouts. Unicode input, translated UI, international filenames and a
complete internationalization of DOS, WIN16 or
emulated guest machines are outside this issue. Their input boundaries still
need regression coverage and an explicit policy before the native map ships.

The first mergeable milestone is optional-module activation, correct ASCII
mapping and layout selection. Accents are a required second milestone:
letter-position changes alone do not complete this plan or justify closing
the issue.

## No kernel growth requirement

**The user requires no net growth in the core kernel.** Moving the feature
to an optional module is a delivery requirement. Passing today's budgets
alone does not satisfy it.

Compare the final stripped core image, its uncompressed code/data/BSS costs,
and the default resident footprint against the implementation's starting
commit, using the same build options, toolchain and build metadata. Report
big, small, emu and mega separately. New hook instructions, dispatch cells,
module rows, settings bytes and glyph routing all count; recover their cost
within the change so the core image and resident totals do not increase.
Report the shipped KERNEL.SYS size too. Existing boot-memory limits and
kern_small's heap floor also remain binding.

BIOS-default boot must acquire no keyboard-module claim and require no new
module read. The selected layout, translator, state and extra glyphs live
outside the core image and consume separately reported RAM only when their
feature is enabled or a document needs the optional glyph provider. Requiring
the same code on every boot, or hiding it in `.cold`, `.mega` or the boot
blob, does not meet this requirement. If a candidate fails the size check,
revise it before proceeding; do not relax the requirement.

## Existing facilities and constraints

| Area | What exists | Consequence |
| --- | --- | --- |
| Typed keys | [`kernel/ui.inc`](../../kernel/ui.inc), `ui_task.keys`, reads BIOS INT 16h and dispatches `AL=character, AH=scan` through `W_ONKEY` | Fix translation before delivery; packages should not each implement a layout. |
| Raw keyboard input | [`kernel/mouse.inc`](../../kernel/mouse.inc), `KBM_SCAN`, `kbm_isr`, `kbm_i15` (§9.7.2) | Reuse the existing XT IRQ1 / AT INT 15h AH=4Fh observation paths. An extra port-60h read can lose a byte on an 8042. |
| Physical controls | `kbd_track`, `OSAPI_KEY_DOWN`, `OSAPI_KEY_MAP` (§9.7) | Keep physical make/break codes and the held-key map unchanged. |
| Protected mode | [`kernel/mega.inc`](../../kernel/mega.inc), `mega_k09`, has its own US character tables | Preserve its default behavior; call the optional module for an explicitly selected layout rather than adding resident international tables. |
| Optional code | [`kernel/mod.inc`](../../kernel/mod.inc), `mod_need`, `mod_drop`, entry slots and module-owned BSS (§2.8) | Reuse the internal module loader, stamps and teardown. All new resident glue counts toward the no-growth requirement. |
| Settings UI | [`kernel/ctrl.inc`](../../kernel/ctrl.inc), `cp_items`, shared drop-downs and deferred save (§31) | Add a Keyboard page using the existing scrolling list and controls. Keep its loading and UI code in the Control Panel module. |
| Persistence | [`kernel/driver.inc`](../../kernel/driver.inc), `CFG_DATA`, `CFG_MAP`, `CFG_BOOT`, `CFG_SAVE` (§51.5) | Add a versioned record to SYSTEM.CFG. kern_small currently reads no SYSTEM.CFG, so its selector must initially be session-only. |
| Desktop glyphs | [`kernel/font.inc`](../../kernel/font.inc), `FONT_FIRST=32`, `FONT_LAST=126` (§6) | Spanish and French accented output needs an optional glyph provider. Private seam glyphs currently use codes 128/129. |
| Text acceptance | [`apps/os88line.inc`](../../apps/os88line.inc), [`apps/os88text.inc`](../../apps/os88text.inc) (§83), and [`apps/notepad/notepad.asm`](../../apps/notepad/notepad.asm) reject bytes above 126 | Audit insertion, rendering and file round trips together. Widening the kernel's output alone leaves accents unusable. |
| Existing glyph source | [`tools/cp437font.py`](../../tools/cp437font.py), [`apps/os88cp437.inc`](../../apps/os88cp437.inc) (§70.8.6) | Reuse the authored glyph shapes where they match; consoles retain their existing CP437 meaning. |
| DOS ownership changes | [`kernel/fsx.inc`](../../kernel/fsx.inc), [`apps/dos/dosw.inc`](../../apps/dos/dosw.inc), [`kerndos/kdkbd.inc`](../../kerndos/kdkbd.inc) (§53, §96.50, §96.54) | A window callback, a fullscreen INT 16h reader and kern_dos do not share one lifetime. Specify each boundary. |

## Proposed design

### Put the feature in an optional keyboard module

Use a §2.8 internal module, proposed filename **KEYBOARD.DRV**, assembled
with each kernel and cut out of the core image by `tools/os88mod.py`.
This uses the existing module mechanism; the `.DRV` suffix does not make
it a new §51 device-driver class or require a second loader.

The module owns translation code, all four explicit layout definitions,
modifier/prefix and dead-key state, composition tables and the extended
glyph provider. Immutable data belongs in its image; mutable state belongs
in its own BSS/claims, with a writable data-segment contract on mega.
Do not leave the language tables or per-layout state in kernel `.text`
or `.bss`. Selection UI and loading requests remain in CTRL.DRV.

Load the module in task context when the user selects an explicit layout,
or during boot when an existing saved preference requests one. Selection
is a deliberate settings operation, so requiring the system disk then fits
§2.8's on-demand rule. Once enabled, input and glyph drawing must continue
with the system disk removed. No keypress or repaint may trigger a file
read, allocation or `mod_need`.

Keep only the necessary dispatch/activation glue in the core, paid for by
equal or greater byte savings on each kernel. The default path bypasses
the module; an absent, invalid or failed module load preserves usable BIOS
input. Do not build a new general-purpose keyboard hook framework merely
to load this module.

Publish validated entry pointers and state atomically. While an IRQ can
enter the module, keep its image/state pinned and callable in that mode.
Disable callbacks and finish outstanding work before a drop or move.
Specify the module's selector, writable data access and real-mode-bracket
handling on mega; never call a protected-mode selector from real mode.
The physical-key map remains the existing kernel facility.

### Translate characters while preserving physical keys

Use one table-driven translator **inside KEYBOARD.DRV**, with shared source
definitions for US, Spanish (Spain), Latin American Spanish and French
legacy AZERTY. Its inputs include the original set-1 scan, prefix state and
the modifiers at that key's arrival. Its result distinguishes pass-through,
consumed modifier/dead key, and one or two translated character words.

The preferred integration point is the existing raw keyboard path and BIOS
key-buffer production. Translate printable keys there, preserve existing
special-key behavior, and publish each character exactly once. This also
covers native fullscreen readers of INT 16h. The XT IRQ1 path, AT 4Fh path
and mega's native handler need narrow adapters to the loaded module.
Wave 1 must prove consumption, BIOS chaining and acknowledgement on all
three paths before this architecture is considered settled.

Do not remap raw scans before `kbd_track`. Do not infer a queued key's Shift
state from the current state when `ui_task` finally reads it. A post-INT-16h
ASCII substitution cannot recover characters the BIOS discarded, distinguish
all AltGr combinations, or reliably handle the ISO extra key at scan 56h.

Pin left/right Shift and Alt, E0/E1 prefixes, Caps Lock, Num Lock, typematic,
logical Ctrl-letter shortcuts and the interaction of Shift with Caps Lock.
Caps behavior is layout data, not a blanket uppercase operation. Right Alt
must be distinguishable from left Alt; the reference decides whether Ctrl+Alt
also selects the AltGr layer. Preserve Alt+Enter, navigation, function keys,
keypad behavior, keyboard-mouse operation and Ctrl+Alt+Del.

Dead-key composition is bounded and allocation-free. Specify accent+letter,
accent+Space, repeated accent, unsupported composition and cancellation.
Reserve enough buffer space before a two-character fallback is published;
retain the existing overrun policy without partial or duplicated output.
Cancel pending composition on layout changes and input-ownership changes,
including focus changes and entry to a foreign session.

### Keep encoding stable across layout changes

The proposed native extension uses **CP850 byte positions** for Spanish and
French characters beyond ASCII. Its mapping is defined by the
[published CP850 table](https://www.unicode.org/Public/MAPPINGS/VENDORS/MICSFT/PC/CP850.TXT).
ASCII, control bytes and the `W_ONKEY` register shape retain their meanings.
Specify the exact supported character set and unsupported combinations;
characters outside it, including the euro sign, need a later encoding feature.

Keep extended glyphs and their lookup/composition code in the optional
module, reached through the existing text renderers and glyph-copy contract
with only the costed core routing described above. Keep character encoding
and glyph meanings independent of the selected keyboard: text entered in
Spanish or French must remain readable after switching to US or BIOS default.
Resolve the private seam-glyph collision explicitly, and cover ROM,
baked and copied-font paths as well as VGA and both monochrome adapters.
Do not enlarge the font range and assume every ROM supplies compatible bytes.

Shared text controls and Notepad must accept, edit, find, save and reopen the
supported bytes. Preserve ASCII-only validation in fields whose domain needs
it, such as network addresses or filenames. Proportional document formats,
PDF fonts and console code pages retain their own contracts; record their
boundaries instead of reinterpreting existing high bytes globally. In
particular, native CP850 input must not be silently displayed as CP437 by DOS
or a terminal. Wave 0 defines conversion or bypass at those boundaries and
keeps broader guest-language support as separately scoped work.

Keyboard activation and glyph use are distinct module holds. A document or
text control needing extended glyphs acquires its hold in task context
before rendering and releases it when the content no longer needs it or
the document/control closes. A reopened accented document must be able to
acquire the provider even with BIOS-default input selected. Account for
borrowed glyph pointers and copies in this contract. Do not load from a
paint callback or keep the module pinned until restart merely because it
was used once.

Selecting BIOS default disarms translation immediately. The glyph provider
may remain held for existing text; once the last explicit layout/glyph hold
ends, disarm every entry and call `mod_drop`, releasing its active claims.
Test the final release, including any existing purgeable-cache policy; no
unexplained live or pinned claim may remain. Register the drop with the
existing module-lifetime checks.

### Reuse settings and module lifetimes

Add a **Keyboard** Control Panel item with BIOS default, US QWERTY,
Spanish (Spain), Latin American Spanish and French AZERTY choices and a short
typing sample. Use the existing drop-down, focus and opaque text conventions.
Switching takes effect atomically for
subsequent keys; already queued characters keep their original interpretation.
Failure to load a layout leaves the previous working selection active.

Persist the choice on big, emu and mega with a new versioned SYSTEM.CFG
record, without resetting unrelated preferences. Keep preference processing
in the boot/settings paths and the active ID in the module; any unavoidable
core staging bytes count toward the no-growth check. Missing, invalid or
unknown records fall back to BIOS default. Reuse the existing dirty flag, save timing
and failure feedback. On small, offer the same live selection and say that
it lasts until restart; adding general settings persistence there is separate
work.

Closing Control Panel must release its own hold without invalidating active
input or glyph clients. Save failure does not unload a working layout.
Document failure/rollback, mode changes, restart and hibernate lifetimes,
including how the optional module is rearmed before accepting keys.

Measure baseline and proposed core bytes with `tools/kernsize.py` and
artefact sizes before selecting an integration. Report the module's file
size and image/BSS/claim costs separately, rounded to the existing 512-byte
allocation granule. Enabling a layout spends optional RAM; the default
configuration must meet the stronger no-growth requirement above.

## Delivery stories

### Wave 0 — Contract and compatibility decisions

- [ ] Add the keyboard-module contract to SPEC.md §2.8 and §9 and update the
  affected portions of §6, §13, §31, §51.5 and §83 **before implementation**.
- [ ] Record a reproducible core-image/resident baseline for all four
  kernels and a byte budget for each proposed hook, slot and settings
  change. Identify the savings that pay for them before implementing the
  complete translator. Adding another layout must add no core bytes.
- [ ] Pin all three international variants, complete scan/modifier vectors,
  encoding, dead-key rules, layout IDs, default and settings-record format. Use the
  [Spanish (Spain) reference](https://learn.microsoft.com/en-us/globalization/keyboards/kbdsp),
  [Latin American reference](https://learn.microsoft.com/en-us/globalization/keyboards/kbdla)
  and [French legacy reference](https://learn.microsoft.com/en-us/globalization/keyboards/kbdfr)
  and their modifier states; retain the precise reference revisions with the
  committed vectors. The
  [keyboard layout samples](https://learn.microsoft.com/en-us/samples/microsoft/windows-driver-samples/keyboard-layout-samples/)
  are a primary source for scan and modifier table structure.
- [ ] Write a compatibility table for native windowed/fullscreen packages,
  DOS prompt and windowed/fullscreen programs, kern_dos, WIN16 and emulated
  guests. Name where translation is active, bypassed or converted, and how
  the desktop layout is restored. Cover mega's real-mode brackets too.
- [ ] Define module entry/state/selector contracts, keyboard and glyph
  holds, IRQ pinning and final drop. Record code/data/stack costs and
  ownership; all new international translation and extra-glyph bodies
  remain outside the core image.
  Define fallback for missing resources and old settings. Update the ABI
  lock if the SDK contract changes; no package-format bump is assumed.

Acceptance: every supported key has a specified result, each ownership
boundary has a policy, and a minimal module integration demonstrates no
net core-image or default resident growth on any shipped kernel.

### Wave 1 — Optional module and ASCII milestone

- [ ] Build KEYBOARD.DRV with the existing module tools and build stamps.
  Put the translator, all explicit layout definitions and mutable state
  in its image/BSS. Ship it separately for each kernel in the supported
  disk geometries; the core image contains none of its payload.
- [ ] Connect the XT, AT 4Fh and mega adapters through the minimal costed
  dispatch, with BIOS-default bypass and safe missing-module behavior.
- [ ] Preserve make/break tracking, buffer ordering, modifier-at-arrival
  semantics and existing special keys. Exercise the ISO 56h key explicitly.
- [ ] Add independent vectors against the assembled guest code, including
  unsupported keys, interleaved modifiers and a full BIOS ring. Expected
  results must not be generated from the implementation's map.

Acceptance: all three international layouts produce the expected ASCII
letters, digits, punctuation and AltGr symbols on all shipped kernel
variants. Physical game controls are unchanged, BIOS default holds no
keyboard module, and core-size/resident deltas remain non-positive.
This is a partial milestone, not issue closure.

### Wave 2 — Selection and persistence

- [ ] Add the Keyboard page using the existing Control Panel table and
  controls, including scrolling when the extra row requires it.
- [ ] Implement atomic selection, failed-load rollback, the typing sample,
  versioned persistence on big/emu/mega and session-only behavior on small.
- [ ] Check both Spanish variants independently: their punctuation/AltGr
  positions must come from separate definitions rather than one alias.
- [ ] Verify boot selection, missing/corrupt data, save failure, switching
  with keys queued and closing/reopening the panel.

Acceptance: a user can select either Spanish variant or French without
rebuilding; the saved selection survives a reboot on kernels with settings
persistence. BIOS-default boot needs no optional module or new module read.

### Wave 3 — Accents and native text round trips

- [ ] Implement the specified Spanish and French extended characters and
  dead keys in the module, including a shared optional glyph provider and
  seam handling with stable encoding across layouts.
- [ ] Implement the glyph-client holds and final release. Verify switching
  to BIOS default with an accented document open, closing the last client,
  and reopening that document with translation still disabled.
- [ ] Update shared text controls and Notepad's insertion, find/replace,
  rendering and save/reopen paths. Audit unsigned-byte classification and
  narrowly fix semantic shortcuts affected by the new layout.
- [ ] Apply the compatibility policies from wave 0, with explicit conversion
  or bypass wherever native bytes meet a different character set.

Acceptance: on each Spanish variant type `ñ Ñ á é í ó ú ü ¿ ¡`, and on
French type `é è à ç ù ê ë`. Edit, find/replace and save/reopen the text;
switch to US and BIOS default and still see it correctly. Check literal
dead accents, invalid compositions and final module release. Inspect the
result on VGA, CGA and Hercules; all extra glyph data/code stays optional.

### Wave 4 — Final verification and contributor guidance

- [ ] Complete the final test pass below, fix failures and record actual
  evidence against every supported kernel and input path.
- [ ] Publish the before/after core-image, uncompressed section/BSS and
  default resident byte table for big/small/emu/mega. Require non-positive
  core deltas independently of `make budget-check`; report enabled-module
  and glyph-client RAM separately, including 512-byte rounding and the
  return to baseline after release. Do not declare completion without it.
- [ ] Add a short keyboard guide, README discoverability and an example of
  adding a layout from independently reviewed vectors. Document character
  and guest-session limits beside the supported behavior.
- [ ] Regenerate docs/INDEX.md after contract changes and update memory and
  heap-claim documentation when their measured values change.
- [ ] Prepare one feature PR with screenshots of the Keyboard page and
  accented text on every affected adapter, hosted at its tested SHA. Link
  this plan and #385. Merge and issue closure require the user's instruction.

Acceptance: all required stories have recorded evidence, including no
kernel growth and optional-module teardown. Any hardware-only result is
identified as pending rather than counted as passed.

## Verification

During implementation, use host builds/checks and a boot to inspect the UI.
Build big, small, emu and mega into isolated trees and name the affected suite
rows in each wave. Do not run soak or test-full per wave.

Add scoped `kbdlayout*` soak rows for assembled module translation vectors,
each Spanish variant, French, native typing, settings/reboot, glyph holds,
activation/drop and the guest-session boundaries. Check that the payload
is absent from the stripped kernel and that BIOS default loads no module.
Register their fixture requirements and measured `secs` under
docs/WRITING-TESTS.md;
use `tools/os88ui.py` and guest-state waits. Include negative controls that
break an A/Q mapping, a Spanish-variant symbol, the AltGr distinction, an
accent glyph, persistence and a module release, and demonstrate that the
appropriate assertion fails.

The final pass runs the deduplicated union **once**, after the last batch:

- New `kbdlayout*` rows plus `kbd4f`, `kbdpreempt`, `kbdburst`, `keymap`,
  `kbmadopt`, `altenter` and `megakbcodes` for the touched keyboard paths.
- `cplistrow`, `cppromise` and `linecar` for settings and shared-field changes;
  `megakbtrip`, `dosaltenter`, `doswfs` and `kdkbd` for affected handoffs.
  Include `moddrop` and `kernmods` for the new module, plus the font rows
  reached by the provider changes.
- `make test-full` and `make budget-check`, including the existing small128,
  kernresident and PS/2 coverage. Include affected knob builds in the final
  union when their conditional paths change.

The final pass also compares all four kernels to the recorded starting
baseline. Check exact core bytes and default resident totals as well as
rounded sizes, so allocation slack cannot conceal added permanent code or
state. Recheck after the final module drop/purge and under memory pressure;
having enough spare budget is not a substitute for this comparison.

Use MartyPC for XT correctness, cycle costs, keyboard-mouse behavior and
VGA/CGA/Hercules captures. Use QEMU for the queued-byte 8042/4Fh case and
PS/2 interaction, and 86Box/Bochs as appropriate for period AT and mega
behavior. Send physical scan sequences rather than host-translated strings;
otherwise the harness can hide the layout bug. Check 83/84-key limitations
separately from the enhanced keyboard's AltGr and ISO key capabilities.

Measure added IRQ work and stack depth, rapid typing during drawing/disk
activity, and the no-layout/default path on the emulated 4.77 MHz XT.
Check restart, hibernate/resume, fullscreen exit, failed-load cleanup and
missing/mismatched KEYBOARD.DRV. Test default boot with the module file
removed and continued typing after the system disk is removed once a
layout has loaded.

Spanish and French hardware validation is useful final evidence, but request
it only after an emulator-tested image and a short, precise checklist are ready;
do not contact the issue author without explicit authorization.
