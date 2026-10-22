# Mixer — delivery plan

A speaker in the menu bar opens an immediate master-volume control when a
hardware mixer is present. An optional native Mixer adds individual source
levels, stereo balance and the card's tone controls. SPEC.md §34.15 is the
contract. [Build diary](../HOW-MIXER-WAS-BUILT.md).

## Stories and waves

- [x] **Foundation:** isolate the worktree, map existing APIs, pin contracts,
  stories and verification, and open one draft PR.
- [x] **Hardware:** as a sound-card owner, I get controls only for the mixer
  actually detected. Queries reflect hardware, unsupported writes refuse,
  stereo settings stay independent, and detach restores borrowed state.
- [x] **Quick volume:** as a desktop user, I see a speaker beside the clock
  and can click, drag or use keys to change volume without opening a window.
  The recessed slider, engraved ticks and ridged thumb look intentional on
  VGA, CGA and Hercules. Dismissal restores the exact covered pixels.
- [x] **Full Mixer:** as an installed-system user, I can open Mixer from the
  popup when SYSTEM contains MIXER.O88 and adjust source levels and balance.
  Removing that file removes the command on the next opening; the 360KB
  floppy keeps the quick control without carrying the optional package.
- [x] **Acceptance:** prove hardware refusals and quantization, popup input,
  package discovery/presence/absence, adapter faces, background playback,
  cleanup and disk/memory budgets; run the affected soak union once and the
  full gate after the last implementation batch.

## Boundaries and verification

Use SOUND.DRV's package verbs, OSAPI_GFX_SAVE/REST, live video geometry,
OSAPI_FILE_FIND/GOTO_Q and OSAPI_PKG_START. The popup belongs to the already
loaded driver; only capability/hit-test/cache glue belongs to the kernel.
No mixer polling in IRQ0 and no new kernel API slots. Shared slider source
belongs to apps/os88mixui.inc; no copied renderer between driver and package.

Hardware matrices: CT1335 (where modeled), SB Pro/CT1345, SB16/CT1745,
SB1/2 without mixer, AdLib only, absent driver, and kern_small. MartyPC is
the XT/adapter instrument; QEMU supplies SB16 behavior where MartyPC's
Sound Blaster model has no mixer. Physical-card audio/analog balance remain
a field-verification limit, not a claim from screenshots.

Final affected union: `mixerhost`, `mixercga`, `mixerherc`, `mixervga`,
`mixersb16`, `hmadrv`, `hmahiber`, `sndmove`, `fmrefuse`, `stikmenu` and
`toastbar`, plus `fpgcold`/`fpgnotch` for the boot-progress interaction,
followed by `test-full`. Host builds and a visual boot were the
per-wave checks. The final union ran after the implementation batch; failed
UI rows were repaired and rerun individually. The SB16 row also checks a
fully staged background stream while the popup stays open.

Result: all 13 affected soak rows and all 63 full-tier rows passed. The full
pass's only initial failure was a missing 128KB VGA machine definition in
the reused emulator assets; that row passed after installing the repository's
definition into a private configuration. The three kernel budgets pass.
The diary records screenshots, resource costs and the physical listening limit.
