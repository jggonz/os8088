# The stream writer, before and after, on every writer it converted (2026-09-29)

**A MEASUREMENT** (docs/README.md): true of the two trees below and of no
other. A later measurement is a new file.

- **Before**: `origin/elendilon` at `492114c`, which carries none of
  docs/plans/STREAM-WRITER-PLAN.md.
- **After**: `stream-writer` with `origin/elendilon` merged in (`4521ae5`)
  plus this report's own commit, which converts the file manager's copy and
  FTPD's STOR. So the one difference between the arms is the stream writer:
  - stage 1's single FAT flush;
  - `OSAPI_FILE_WRITE_SEQ`, plain and HELD (SPEC.md 18.4.9);
  - the banked FAT across a hop (18.8.5);
  - the three converted writers (22.23.5's join, 22.5.3's copy, 77.49's STOR).
- **Box**: MartyPC headless, four lanes on a 4-core container. Every figure is
  GUEST seconds off the machine's own cycle counter (4.77 MHz), so lane
  contention cannot move it (docs/plans/SOAK-PARALLEL.md 1). No breakpoints
  were set in these runs (`--nobp`). Every run verified its result byte for
  byte, and a clean FAT, on the host.

## The table

| writer | scenario | before | after | change |
|---|---|---|---|---|
| Uncompress To... (the join) | 640 KB set, B: to A:, two 1.44 MB floppies | 133.4 s | **117.4 s** | -12.0% |
| Uncompress (the join) | 640 KB set, beside its parts on B: | 125.0 s | **102.4 s** | -18.1% |
| Uncompress To... (the join) | 300 KB set, 360 KB B: to C: (XT-IDE) | 41.5 s | **39.9 s** | -3.9% |
| Copy / Paste | 640 KB file, B: to A: | 80.3 s | **72.4 s** | -9.8% |
| Copy / Paste | 640 KB file, B: to another folder on B: | 80.6 s | **64.9 s** | -19.5% |
| Copy / Paste | 300 KB file, 360 KB B: to C: (XT-IDE) | 32.3 s | 32.5 s | none |
| FTPD STOR's calls | 12.5 MB to C:, WRITE then 8 KB appends | 1,746 s | **258 s** | **-85.2%** |
| VIDDISK W (the bench) | 12.5 MB to C:, 32 KB appends | 577 s | **206 s** plain, **193 s** HELD | -64% / -67% |

The joins and the copy were driven through the real surface (a menu verb, a
Save box, Ctrl+V) by `tests/czseq.py --nobp --verb {to,same,copy,copyb}
[--hdd]`, run in both trees. The last two rows are `tests/viddisk.py --floppy`
(`--wmode seq|held` after).

## What the floppy controller did

MartyPC's controller counts are taken from outside the guest (`m.disk()`),
so they need no instrument in either kernel.

| scenario | writes before -> after | head travel (cylinders) before -> after |
|---|---|---|
| Uncompress To..., B: to A: | 190 -> 100 | **2,111 -> 345** |
| Uncompress, on B: | 190 -> 100 | 2,217 -> 1,627 |
| Copy, B: to A: | 135 -> 89 | 1,111 -> 261 |
| Copy, within B: | 135 -> 90 | 1,716 -> 876 |

Head travel is the sound the owner reported during a join. Between two
floppies it falls by 84% and 77%. Within one floppy the data and the FAT are
still on the same disk, so it falls less.

## Reading it

**FTPD's row is the one the plan started from.** An 8 KB append that looks
its name up and walks its chain from the front makes an upload quadratic in
its length. docs/plans/DISK-CPU-PLAN.md 6 ESTIMATED ~230 s of walking in a
5 MB STOR; at 12.5 MB it is most of 1,746 s. The row is FTPD's exact
sequence of kernel calls, not FTPD itself: MartyPC has no network card, so
VIDDISK makes the same `OSAPI_FILE_WRITE` and then the same 8 KB calls from a
far buffer to the same fixed disk. FTPD itself was checked under QEMU
(`tests/ftpd.py`, all assertions, a 20,000-byte STOR read back off the image
by an independent reader). **Its network path is untouched**, and SPEC.md
77.21 found that path, not the disk, to be what bounds a small upload at
~7 KB/s. So the gain shows as a file GROWS, and a short upload will not feel
it.

**Two rows do not move, and both are the XT-IDE.** MartyPC's fixed disk
takes ~6 ms a call and models no seek, and at 300 KB the source floppy is
most of the time. The target's metadata calls per 32 KB block fall from 4 to
0.6 (counted with breakpoints, `tests/czseq.py --hdd`), so what that is worth
on the owner's ST-225 is a PREDICTION: two long seeks to cylinder 0 and back
per block. It is for 86Box's `pc5150` or the 5150 itself to confirm.

**The join floppy-to-floppy has a cost the arithmetic missed** (plan 11). The
ROM spins up only one floppy motor at a time and waits it out before a
write, and the old per-block lookup was a read that spun the target up for
free. Its gain here is the FAT flush the bank takes off each hop, not the
lookup it removes.
