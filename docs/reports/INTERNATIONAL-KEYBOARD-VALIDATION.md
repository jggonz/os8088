# International keyboard validation

Core implementation: `c8a3160b9`, unchanged by the subsequent upstream Wire
merge, documentation, captures and keyboard-test premise correction.
Comparison build number: 343; assembler: NASM 3.02. Every final core size
and hash matches [keyboard-final.json](keyboard-final.json).

## Final results

| Gate | Result |
| --- | --- |
| Scoped host/QEMU | 8 passed |
| Scoped native UI, frozen snapshot, verified MartyPC | 30 passed, 0 failed, 0 skipped |
| Scoped Bochs | 6 passed, 0 failed |
| Scoped 86Box | 1 passed, 0 failed |
| `OS88_BUILDNUM=343 OS88_BOOTCACHE=0 make test-full` | 76 passed, 0 failed, 5 declared skips, 37.1 seconds |
| `OS88_BUILDNUM=343 make budget-check` | big/small/emu/mega passed, budgets unchanged |
| Final A/B size and hash recapture | exact match to committed evidence; no positive core/permanent/resident/shipped delta |

The five full-tier skips are `ctoolchain`, `pdlay`, `pd360`, `pdnotes` and
`pded`, requiring the optional C toolchain or PageDesk artifacts.

The 45-row scope is the deduplicated union below. The corrected native
pass used `OS88_BOOTCACHE=0`, `--frozen`, `-j 3 --marty-jobs 3`; the frozen
source was `d5eed78670`. Bochs ran in an isolated Linux container. The
final 86Box row includes the prerequisite correction in `a7a6c1b41`.
Full verification ran alone after those jobs completed.

| Group | Rows |
| --- | --- |
| Host/QEMU | moddrop, kernmods, keymap, kbdlayouts, kbdlayoutfont, kbdlayouttransitions, kbd4f, kbdlayoutat |
| Native keyboard/text | kbmadopt, kbdburst, kbdpreempt, kbdlayoutabort, kbdsettings, kbdsettings-small, kbdlayouttext-vga, kbdlayouttext-cga, kbdlayouttext-herc, kbdlayouthiber, kbdlayoutlegacy, kdkbd, doswfs, linecar, altenter, dosaltenter |
| Native modules/files/UI | rehomeabort, fcpapi, fcpcopy, fcpsmall, cppromise, diskmut, fontpick, lzcomp, czjoin, czto, bigdir, modcache, cplistrow, imgmount |
| Bochs | megafont, megakbtrip, megamodcache, kbdlayoutreal, mega-kbdlayouttext-vga, mega-kbdlayouthiber |
| 86Box | megakbcodes |

Use repeated `-k ROW` arguments with `python3 tools/os88test.py soak` to
select this scope; do not request the whole soak tier. `tests/suite.py`
registers every fixture prerequisite and measured runtime. New keyboard
fixtures can also be built through `kbdlayoutaborttest`,
`kbdlayoutrealtest`, `kbdlayoutlegacytest` and `diskmuttest`.

## What was exercised

Independent actual-instruction vectors cover 375 ordinary key facts per
kernel, plus accents, AltGr/Ctrl+Alt, Caps/Shift, ISO 56h, logical Ctrl,
physical-map preservation, foreign ownership, ring wrap/fullness and
unsupported composition. Raw execution peaks at 26 stack bytes, including
the FAR frame, under its 48-byte contract. The AT test checks batched
8042 make/break consumption without duplicate words.

Native UI checks cover physical ES/LA/FR input, all five choices,
save/reboot and malformed records, missing/mismatched modules, preserved
unrelated clock settings, the 128KB session-only selector, failed save,
continued typing/repaint with the system disk swapped out, two Note Pad
clients, accented find/replace, independently decoded FAT file bytes,
save/reopen/New and final drop. Failed entry cleanup and instance reuse
are exercised with actual packages.

Explicit layout and glyph-only ownership survive actual XT and Mega
hibernate/resume. A high-memory package proves protected/real/protected
glyph borrowing and restored CP850. DOS window/fullscreen and actual idle
WIN16 entries retain BIOS words; focus and a synthetic owner-name change
preserve the RAW contract. Directory mutation has twelve public-API
success/refusal cases with register, volume/CWD, write-gate, hold-lifetime
and independent FAT checks. Missing/mismatched modules leave both disk
images byte-identical.

Final XT KL_SCAN call/return samples: unloaded 118–124 cycles; Spain
1,475–2,953, Latin America 1,482–2,965, French 1,481–6,911. The French
sample includes unsupported dead-key fallback. The maximum is about
1.45 ms at 4.77 MHz; these are observed cases rather than a claim about
every physical XT.

## Negative controls and instrument repairs

Deliberate controls catch wrong French/Spanish/AltGr table entries,
a damaged accent glyph, missing CLI, wrong persisted choice, missing
client RELEASE, missing failed-entry cleanup, hibernate header overwrite,
a lost Mega instance stamp and missing real-mode bounds clamp. Clearing
DOS's actual RAW flag fails the required BIOS-byte vector. Corrupting the
native Mega keypad table fails Ctrl+keypad-5 with the real pointer active.

The initially copied MartyPC binary omitted keyboard hold/pacing patches;
both byte-identical starting and feature kernels failed its burst test.
Those stress results were discarded. The exact repository pin plus all
17 patches was rebuilt; unchanged original burst, preemption, caret and
image-folder tests pass. [Instrument provenance](keyboard-instruments.json)
records the binary, patch and machine-configuration hashes. An initial
full-gate launch lacked the VGA128 machine profile; synchronizing the
verified bundle and rerunning the full gate resolved it.

The initial parallel prebuilds also produced transient image/map mismatches.
The final native pass uses its own frozen artifacts. The 86Box matrix now
establishes a genuine serial mouse first: keyboard-mouse fallback otherwise
owns keypad 5 on both the baseline and feature kernel.

## Evidence and limits

[Committed screenshots](../keyboard/README.md) show the Keyboard page and
physical three-layout Note Pad text on VGA, CGA and Hercules, plus Mega
text under Bochs. Native captures were refreshed after instrument repair.

No physical Spanish/French keyboard run is claimed. WIN16 checks its idle
entry and a synthetic rename, not execution of a reference NE application.
DOS/WIN16 language input, modern AZERTY, Unicode and international filenames
remain outside this feature. Rename, New Folder and folder removal now
require the matching system-disk DISKOPS module before mutation; this is
the user-visible cost of paying for all resident keyboard glue.
