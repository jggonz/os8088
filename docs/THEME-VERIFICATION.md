# Color preset correction verification

The correction preserves Classic, Bright and Dark. Current preset IDs are
contiguous 0..7 in visible order; only Classic retains its original ID 0.
The other IDs were explicitly renumbered as requested. Ocean and Windows 95
are absent, with no reserved compatibility rows. The TP record remains a
version-1 byte. The successive measurements below record each implementation
stage; the current compact layout is described in the final section.

The pre-PR audit below supersedes the historical size figures. Earlier
sections retain the measurements of the nine-preset development versions.

Inactive-title and menu-selection colors are separate semantic roles in the
existing table. There is no palette programming, RGB approximation, new
framebuffer, heap allocation or application-content role. Existing white
controls and application content retain their intentionally unthemed behavior.

## Executed checks

`tests/colorpresets_qemu.py` boots the actual built disk, verifies the symbol
map against kernel.bin, and calls existing guest entries from the UI task.
CPU state and its temporary debugger trampoline are restored after each call.
Guest disk writes go only to a test copy. Screenshots come from QEMU's video
output; the test permits only the BIOS DAC's small RGB quantization differences.

The VGA mode 12h and forced EGA mode 10h checks cover all eight Color presets:

- Desktop, active/inactive titles, menu paper and enabled selection colors.
- File Manager and the real APPS folder retain white interiors.
- Control Panel and Customize retain white content/control areas.
- Existing Paint content, including its drawing palette, is pixel-identical
  between presets; the WM-owned resize grip is excluded as chrome.
- Existing Notepad's document view stays white and pixel-identical.
- Bright/Dark keep their menu behavior and the same Paint content.
- Customize opens with all eight presets and Solid, Stripes and Windows title
  styles. The Windows-only **95** checkbox restores the main-branch Windows
  button colors while retaining the selected preset's title-bar colors.
  Toggle, Apply/Cancel, and reboot persistence use the existing style byte.

All eight presets also passed actual Control Panel settings writes and a fresh
boot confirming the Color mode and preset ID. Windows title-bar and 95 button
styles persist in the existing style byte while the saved theme remains Color.
The kernel-small CGA mode
06h smoke test boots File Manager, confirms black/white rendering and proves
the Color API cells leave pixels and the mode unchanged.

Commands, using the installed NASM and QEMU paths on Windows:

```text
make build/os8088-360.img
python tests/colorpresets_qemu.py --qemu <qemu-system-i386>
make VIDEO=ega build/os8088-360.img
python tests/colorpresets_qemu.py --ega --qemu <qemu-system-i386>
```

`--visual-only` skips the already verified save/reboot phase. For CGA,
build `BUILD=build/smallk KERN_SMALL=1 VIDEO=cga build/smallk/boot360.bin`,
create a small-kernel disk with its generated modules, set OS88_BUILD to that
directory, and run the same test with `--mono-smoke --image <small-disk>`.

Additional checks passed: overlay segment/call checks, documentation/index,
ABI lock, 958 published API checks, kernel addressing/layout and 454 mirrored
constant checks. FAT12 verification passed for the generated 360 KB disk.
The ABI lock now records the customization API's compatible appended cells;
neither PKG_FMT nor DRV_VER was bumped and no previous slot was moved.

## Measurements

Measured with `tools/kernsize.py --json --strict` against the committed
customization implementation before this correction, with the same assembler:

| Measurement | Before | After | Difference |
|---|---:|---:|---:|
| Big-kernel resident `.text` (code and initialized data) | 44,519 | 44,747 | +228 bytes |
| Big-kernel `.bss` | 5,134 | 5,134 | 0 |
| Big-kernel rounded RAM footprint (KERN_SIZE) | 99,328 | 99,840 | +512 bytes |
| Small-kernel `.text` | 32,944 | 32,944 | 0 |
| Small-kernel rounded RAM footprint (KERN_SIZE) | 65,024 | 65,024 | 0 |

Big-kernel `.cold`, boot-overlay and low-BSS sizes are unchanged. The table
grows by 56 bytes (one restored preset plus four added role bytes per row),
and the live role block by four bytes. KERNEL.BIN is 98,707 bytes and packed
KERNEL.SYS is 84,384 bytes. After the Amber caption was shortened, THMPAL's
image is 1,369 bytes and its packed file 1,258 bytes. The image matches the
previous chooser's size, within the existing rounded overlay claim. The
360 KB image uses 314 of 354 data clusters.

## Limits

QEMU emulates a VGA running the real EGA BIOS mode 10h, not a physical EGA
adapter. MartyPC and physical EGA/Hercules hardware were unavailable. Hercules
was compiled through the existing small-kernel path but not executed. The
tests cover the listed system windows, controls and existing applications;
they do not claim an exhaustive check of every application or modal dialog.

## Color title-bar styles

Customize offers **Solid**, **Stripes** and **Windows**. Solid and Stripes use
the existing Color title painter and the persisted TS preference. Windows
selects the existing on-demand WIN95.DRV theme kind. Switching among the three
updates the live title bars; Cancel restores the opening mode and Apply keeps
the choice. Bright and Dark retain their existing stripe appearance.

The QEMU test exercises each radio callback, verifies the live theme kind and
stripe state, and checks Cancel, Apply and preset changes. Color preferences
and Windows/95 styles are verified through settings writes and fresh boots;
the saved mode remains Color. White
window content and existing Paint/Notepad pixels remain unchanged.

Relative to the solid-title correction, the big kernel adds 54 bytes of
resident text/data, one BSS byte, six cold bytes and 13 boot-overlay bytes.
The small kernel adds nine resident text/data bytes; rounded kernel RAM is
unchanged on both variants. The chooser image is 1,603 bytes (1,463 packed),
still within its existing 2 KB overlay claim. KERNEL.BIN remains 98,707 bytes;
packed KERNEL.SYS is now 84,432 bytes. No new allocations or framebuffers are
introduced. The chooser is 200x176 pixels and fits the EGA desktop.

## Initial preset list order (superseded IDs)

Ocean was subsequently removed. The visible order is Classic, Workbench,
Light, Forest, Amethyst, Amber, Midnight and Desert, mapped to unchanged IDs
0,7,9,3,4,5,6,8. Removed Ocean ID 2 falls back to Classic. The test clicks
every visible row and verifies its live preset ID, then exercises the stripes
checkbox and Cancel/Apply in the shorter window. All eight presets passed the
VGA content/color checks. The chooser is now 200x165 pixels, with a 1,597-byte
image and 1,457-byte packed file, within the existing overlay claim.

## Current IDs and Color Theme title

Preset IDs are now Classic 0, Workbench 1, Light 2, Forest 3, Amethyst 4,
Amber 5, Midnight 6 and Desert 7. The two reserved rows, ID mapping array
and reverse lookup are removed. Old non-Classic development IDs are not
migrated; out-of-range values fall back to Classic.

The chooser title is **Color Theme**. Its shorter caption leaves visible
pinstripes on both sides; the test verifies stripe ink and background pixels
in this window itself, in addition to the underlying Paint title. Clicking
all eight visible rows verifies the matching contiguous IDs. The chooser
image was 1,560 bytes (1,420 packed) before the title-style selector; the
current build remains within the existing overlay claim and uses a 200x218
window, with a visible gap before the style controls and action buttons.

## Pre-PR audit against main (2026-10-13)

Reviewed the entire tracked difference between local main `2f6a0a8` and
feature head `fe03002`, plus the three uncommitted Windows host-tool changes:
kernel/theme resolution, menu and window painting, Control Panel callbacks,
driver loading, API cells, configuration layouts, THMPAL, disk/build rules,
SDK/ABI documentation, indexing and the guest verification script. The audit
correction is committed separately from the original feature implementation.

### Corrections and reductions

- The public mode API now posts repaint and settings flags, matching its
  existing SDK contract. The internal boot resolver remains separate. Prior
  tests forced painting and therefore missed stale chrome after an API call.
- Preset and stripe setters share resolution/posting code. Removed a redundant
  jump and unused color equates; existing public slot addresses are unchanged.
- Table/live rows shrink from 12 to 10 bytes: selected ink is always white
  on the colored menu path, and the alignment byte served no purpose. The
  resolver copies five words instead of six. A compile-time assertion checks
  that the live role block matches the table stride.
- A row click no longer redraws all eight labels immediately before the
  already posted UI repaint. GDB breakpoints counted eight `font_run_x` calls
  in the pre-audit row callback and zero in the corrected callback. The old
  overlay was run on the test kernel with only its private ABI word adapted;
  the regression test also verifies the resulting normal UI repaint pixels.
- Bright/Dark Cancel restores the mode before its hidden Color preferences;
  a breakpoint confirms one resolver call rather than the previous three.
- Menu unselection shares its painting tail and avoids testing the legacy
  predicate twice. A register exchange replaces a stack save/restore when
  staging the menu background pen.
- Closing the Control Panel now destroys any remaining chooser before
  releasing its image. The module stays cached between chooser openings
  while the panel is open, avoiding repeated floppy reads. Failed window
  creation restores the original appearance. Private overlay ABI 2 is
  checked before dispatch; a mismatch is released and reported as DRVE_BAD.
- Windows builds now retain a real cross-process byte lock, including while
  reading build products. Same-process reentry works; another process waits
  until the holder exits. POSIX retains its shared-reader flock behavior.
- GDB socket EOF now fails promptly instead of looping forever. Dr Mario's
  explicit Latin-1 decoding preserves ASCII address/byte annotations across
  hosts; asset and music outputs match main with the same explicit decoding.
- Theme and lock tests are registered in the soak tier. The theme test uses
  the suite's build-path resolver and registers QEMU teardown, including on
  SIGTERM. Its measured full VGA run is 72 seconds on this host.

### Space and RAM

NASM measurements use the same generated includes and definitions for all
three source snapshots. These are the default big/small variants, rather
than forced-video measurements. `docs/KERNEL-MEMORY.md` was regenerated.

| Measurement (bytes) | main | Before audit | After audit |
|---|---:|---:|---:|
| Big resident text/data | 44,331 | 44,768 | 44,725 |
| Big BSS | 5,133 | 5,135 | 5,135 |
| Big cold code | 37,938 | 38,022 | 38,042 |
| Big boot overlay | 2,470 | 2,496 | 2,496 |
| Big rounded KERN_SIZE | 99,328 | 99,840 | 99,840 |
| Small resident text/data | 32,927 | 32,953 | 32,953 |
| Small rounded KERN_SIZE | 65,024 | 65,024 | 65,024 |

The audit saves 43 resident bytes; compatibility/lifetime checks add 20 cold
bytes, so total unrounded kernel sections shrink by 23 bytes. Rounded RAM
does not shrink: the whole feature still costs one additional 512-byte image
rung against main. Big BSS increases only two bytes against main; small BSS,
cold code and overlay are unchanged. Budgets and minimum-RAM limits remain
unchanged. No new guest allocation, framebuffer, bitmap or palette remapping
was introduced by the audit. THMPAL remains within its existing 2 KB claim.

The final default KERNEL.BIN is 98,707 bytes; packed KERNEL.SYS is 84,400.
THMPAL is 1,562 bytes unpacked and 1,422 packed, two bytes larger than before
the audit because it also handles parent teardown and failed creation.
The 360 KB disk still uses 314 of 354 data clusters. Correctness checks take
priority over reducing the packed kernel's 16-byte increase from 84,384.

### Verification and limits

All eight presets passed VGA mode 12h and EGA mode 10h desktop/title/menu,
white File Manager/folder/Control Panel, selection, stripes, Apply/Cancel,
existing Paint/Notepad content, configuration save and fresh-boot checks.
The corrected mode API and row preview are tested through the normal UI loop,
without forcing a repaint. Parent teardown and private ABI rejection passed.
Small-kernel CGA mode 06h passed its boot/File Manager/API-no-op pixel check.
ABI, mirror, layout, 8086 assembly rules, overlay-call, test registration,
QEMU ownership, documentation/index and FAT12 checks passed; the new host-lock
concurrency test passed on Windows. Failed window creation was also injected
and confirmed to restore Bright and release the module.

QEMU does not measure 8088 execution time. The runtime improvement is reported
as counted work, not a millisecond or percentage claim. It runs actual EGA
BIOS mode on an emulated VGA; physical EGA/Hercules and MartyPC were unavailable
to the automated audit environment. The contributor subsequently reported a
quick real-hardware test with no noticeable performance difference in the
original Color/Classic appearance. This is a preliminary qualitative result,
not a timed benchmark; hardware details and a longer comparison against main
remain to be verified before merge.
No assertion is made that every possible application/dialog was exercised.
An already untracked malformed temporary-build directory was left untouched
and is outside the source diff and proposed correction.

## Re-audit after #252 integration and chooser refinements

The measurements above describe the earlier comparison against the then-local
`main`; that branch did not yet contain upstream PR #252. For the current
feature, `b47de145` (the #252 integration snapshot) is the fair baseline. Both
snapshots were assembled with the same local toolchain and `tools/kernsize.py`.

| Bytes | #252 baseline | Current feature | Change |
|---|---:|---:|---:|
| Big resident `.text` | 43,726 | 44,176 | +450 |
| Big `.bss` | 5,214 | 5,216 | +2 |
| Big cold code | 38,400 | 38,520 | +120 |
| Big boot overlay | 5,097 | 5,097 | 0 |
| Big low BSS / video buffer | 5,598 / 336 | 5,598 / 336 | 0 |
| Big boot2 | 4,857 | 4,902 | +45 |
| Big rounded `KERN_SIZE` | 98,816 | 99,840 | +1,024 |
| Small resident `.text` | 31,865 | 31,894 | +29 |
| Small `.bss`, cold, boot2, overlay | 3,063 / 22,442 / 4,341 / 1,480 | same | 0 |
| Small rounded `KERN_SIZE` | 62,464 | 62,464 | 0 |

The measured sections total +617 bytes, including 45 bytes in `.boot2`, which
is temporary boot code and is returned at `spl_finish`. The sections charged
to the resident rungs grow by 572 bytes; the image and cold segments each
cross a 512-byte allocation rung, so `KERN_SIZE` grows by 1 KiB. The large
kernel remains 1 KiB below its 100,864-byte budget; the small kernel retains
1,536 bytes of budget headroom. Raw `KERNEL.SYS`
grows from 97,769 to 98,793 bytes; its packed form grows from 84,768 to
85,232 bytes (+464). `CTRL.DRV` grows by 124 bytes (12,355 to 12,479).

`THMPAL.DRV` is not resident at boot. Its 2,435-byte image is claimed only
when Customize opens; the allocator rounds that claim to 3 KB. The 2,106-byte
compressed file is what is read from disk. The overlay remains cached while
the Control Panel is open and is released when that parent closes; this trades
3 KB of live heap for avoiding another floppy read if Customize is reopened.
The window uses the existing window pool and allocates no framebuffer or theme bitmap.
Color state adds two bytes of preset/style settings and semantic rows in the
large kernel; settings still use the existing configuration mechanism. The
124-byte CTRL.DRV and 22-byte WIN95.DRV growth remains inside their existing
13 KB and 2 KB runtime claims, respectively. Across KERNEL.SYS, CTRL.DRV,
WIN95.DRV and THMPAL.DRV, the installed binary payload grows by 2,716 bytes
before FAT cluster rounding and saved configuration records.

The redraw audit found that a Color preset click now relies on the one repaint
posted by the theme setter; it does not redraw the chooser's eight labels
before that repaint. Bright, Dark and Classic retain the legacy XOR selection
path. Other Color selections repaint only the menu item cell. The eligibility
check now reads the already resolved selection-color role directly instead
of separately testing theme, preset and adapter on each hover. Entering the
Windows title style and changing a preset while Windows is active each call
the theme resolver once; regression breakpoints assert both counts. UI posts
are zero/non-zero flags, so successive state setters in one gesture coalesce
into one UI pass. A theme/preset change still requires that pass to update the
desktop and every affected window title.

The latest checks passed the complete VGA suite, including persistent writes
and fresh boots for all eight presets plus Windows/95 style. The visual suite
also passed mode 10h for all presets, with white interiors and unchanged
Paint/Notepad content. API ABI (968 checks), ABI lock (12), mirror (456),
overlay-call checks and `git diff --check` passed. QEMU confirms rendered work
and call counts, not 8088 wall-clock speed; the earlier quick physical test
remains qualitative. The semantic-selection fast path is Big-kernel only;
the small CGA build stays on its original XOR path.
