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

Pending implementation. Before completion, build the head and baseline with
identical `OS88_BUILDNUM=343`, toolchain and options, retain the stripped
images and maps, and compare every column above. Report section deltas as
well as totals; no positive permanent/core/resident delta is permitted.
Do not substitute budget headroom or rounding slack for this comparison.
Record optional module image/BSS and active claims, glyph-only holds, and
the return to the default footprint after the last hold is released.
