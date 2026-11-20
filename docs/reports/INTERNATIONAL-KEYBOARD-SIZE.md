# International keyboard size evidence

The user requires no net growth in the core kernel for issue #385.
The baseline is core source `b82e4e94310e`, with contract-only foundation
`c3526d626`, NASM 3.02 and build metadata 343. The contract commits change
no kernel code. Full figures and hashes are in [keyboard-baseline.json](keyboard-baseline.json).

## Baseline

| Kernel | Stripped kernel.bin | Shipped kernel.sys | Unrounded permanent bytes | Resident bytes | HEAP_SEG |
| --- | ---: | ---: | ---: | ---: | --- |
| big | 106,000 | 84,480 | 90,376 | 95,232 | `17A0h` |
| small | 63,328 | 53,072 | 56,307 | 57,344 | `0E60h` |
| emu | 106,352 | 84,704 | 90,622 | 95,744 | `17C0h` |
| mega | 146,497 | 117,040 | 129,718 | 61,440 | `21C0h` |

The unrounded permanent total adds the built map sizes of `.text`, `.bss`,
`.cold`, `.vga`, `.mega`, `.lowbss` and `.vgabuf` where present, plus
`STK0_SIZE`. It excludes the boot-only sections and extracted module
images/BSS. Resident bytes are `KERN_SIZE` on big/small/emu and
`MEGA_RESIDENT` on mega. Enabled optional claims are a separate measurement.

The baseline normal build passed: 53 fast rows passed, none failed, and
four rows requiring optional C/PageDesk artifacts skipped. `make small emu
mega` also completed. These are host build observations, not emulator
verification of the feature.

## Final comparison

The final artifacts are from source `c8a3160b9`, NASM 3.02 and
`OS88_BUILDNUM=343`, matching the baseline metadata and toolchain. Values
below are **baseline → final (delta), in bytes**. Full sizes and core hashes
are in [keyboard-final.json](keyboard-final.json).

| Kernel | Unrounded permanent | Stripped kernel.bin | Shipped kernel.sys | Static resident |
| --- | ---: | ---: | ---: | ---: |
| big | 90,376 → 90,190 (-186) | 106,000 → 105,824 (-176) | 84,480 → 84,320 (-160) | 95,232 → 95,232 (+0) |
| small | 56,307 → 56,153 (-154) | 63,328 → 63,200 (-128) | 53,072 → 52,912 (-160) | 57,344 → 57,344 (+0) |
| emu | 90,622 → 90,436 (-186) | 106,352 → 106,176 (-176) | 84,704 → 84,576 (-128) | 95,744 → 95,232 (-512) |
| mega | 129,718 → 129,665 (-53) | 146,497 → 146,481 (-16) | 117,040 → 116,960 (-80) | 61,440 → 61,440 (+0) |

Every permanent, stripped-core, shipped and resident total is at or below
the baseline. The comparison asserts this for every kernel; it does not
use allocation slack as evidence. The heap segment stays `17A0h` on big,
`0E60h` on small and `21C0h` on mega; emu drops from `17C0h` to `17A0h`.
The small kernel's strict heap floor remains intact. Mega's static resident
column measures conventional memory; its permanent total also counts
`.cold` and `.mega` above 1 MB.

The permanent sections are measured individually, with an absent section
counted as zero:

| Section | big | small | emu | mega |
| --- | ---: | ---: | ---: | ---: |
| `.text` | 39,342 → 39,497 (+155) | 28,960 → 29,112 (+152) | 39,588 → 39,743 (+155) | 44,943 → 45,138 (+195) |
| `.bss` | 4,882 → 4,891 (+9) | 2,831 → 2,843 (+12) | 4,882 → 4,891 (+9) | 4,920 → 4,929 (+9) |
| `.cold` | 37,728 → 37,378 (-350) | 21,136 → 20,818 (-318) | 37,728 → 37,378 (-350) | 48,639 → 48,350 (-289) |
| `.vga` | 2,218 → 2,218 (+0) | 0 → 0 (+0) | 2,218 → 2,218 (+0) | 0 → 0 (+0) |
| `.mega` | 0 → 0 (+0) | 0 → 0 (+0) | 0 → 0 (+0) | 24,768 → 24,800 (+32) |
| `.lowbss` | 5,358 → 5,358 (+0) | 2,868 → 2,868 (+0) | 5,358 → 5,358 (+0) | 5,600 → 5,600 (+0) |
| `.vgabuf` | 336 → 336 (+0) | 0 → 0 (+0) | 336 → 336 (+0) | 336 → 336 (+0) |
| `STK0_SIZE` | 512 → 512 (+0) | 512 → 512 (+0) | 512 → 512 (+0) | 512 → 512 (+0) |

The kernel hooks and font routing consume some `.text`, `.bss` and, on
mega, `.mega` bytes. Moving Rename, New Folder and folder removal into the
existing `DISKOPS.DRV` pays for them: the isolated split removed 509 cold
bytes on big/emu/mega and 482 on small. Its optional image grows by 904
bytes on big/emu/mega and 825 on small. These directory commands now need
the system disk's matching `DISKOPS.DRV` when first invoked; a load refusal
leaves the attempted mutation untouched. The loader restores the target
volume and directory before executing it. The existing drop/cache policy
ends the operation's hold.

A separate boot settings-reader compaction removes 46 actual `.boot3`
code bytes on big/emu/mega by eliminating redundant register saves beneath
the reader's outer frame. It changes no permanent or optional-module bytes.
The final stripped-image figures above include that saving and the existing
alignment rules. No default boot claim was added.

## Optional keyboard memory

The translation tables, dead-key state and CP850 font provider are in
`KEYBOARD.DRV`. The shipped `kernel.bin` and `kernel.sys` exclude that
module; the full NASM image contains it only as input to module extraction.
The runtime allocation is separate from the static kernel figures above:

| Kernel | Code/data image | Module BSS | Total runtime bytes | Heap claim | Compressed .DRV file |
| --- | ---: | ---: | ---: | ---: | ---: |
| big | 5,594 | 16 | 5,610 | 5,632 (5.5 KiB) | 2,906 |
| small | 5,594 | 16 | 5,610 | 5,632 (5.5 KiB) | 2,906 |
| emu | 5,594 | 16 | 5,610 | 5,632 (5.5 KiB) | 2,906 |
| mega | 5,634 | 16 | 5,650 | 6,144 (6 KiB) | 2,923 |

The 16 BSS bytes are emitted as the module's zeroed tail, so the extracted
uncompressed module size equals its total runtime bytes. The heap rounds
that total to 512-byte granules: **5.5 KiB on big/small/emu, 6 KiB on mega**.
The compressed file size is a disk cost, not the runtime claim size.

One claim serves every explicit layout and every native glyph holder.
BIOS default with no glyph holder has no `KEYBOARD.DRV` claim. Selecting an
explicit layout or acquiring the first glyph hold loads and pins it; input
and painting then use the loaded tables. With BIOS selected, releasing the
last glyph hold restores the original font, disarms the callbacks and frees
the claim. The Control Panel's own optional module is accounted separately.

The final calibration runs checked clean-close release of the BIOS-only
accented sample, failed-entry HOLD cleanup and a fresh HOLD/RELEASE in the
reused slot. Transition checks covered explicit and glyph-only holds
through hibernation, fullscreen use and mega's real-mode bracket. Directory
mutation checks covered success and missing/mismatched `DISKOPS.DRV`,
including returned registers/CF, target volume/CWD, hold cleanup and host
FAT verification. These are the recorded lifecycle observations; this size
report itself only reads the already built maps and images.
