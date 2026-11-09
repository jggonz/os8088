# The boot canary and the Pravetz report

The owner's question (2026-11-08): *"The broken boot canary: Pretty critical,
we do know of a single system, a pravada 8088, that can't use floppy cyl
reads. They report booting fine but longer, though, currently - so we probably
broke this recently?"*

The short answer: **no**. The canary on the 9-sector disks was not broken
recently. It has been inert since **2026-09-05**, the day the packed kernel
became the default. And "boots fine but longer" is what a machine looks like
when one of the two rescues is WORKING, not a sign that one stopped. If that
machine were the kind the canary exists for and it booted a 360KB or 720KB disk,
it would not boot fine today. BR-1 stands, but its date, one of its
consequences and its price all change below.

Method: the code, the records and `git log` at this tree (`the-big-review`,
unshallow), plus a host model of the loader's run split over the BPBs of the
built images in `build/`. Nothing was built, booted or edited except this file.
Anything marked MODELLED comes from that model. Anything marked PREDICTED has
not been run.

---

## 1. What the records say about this machine

**No record says a Pravetz can't do cylinder reads.** `grep -i 'pravet\|pravada'`
over `docs/`, `SPEC.md`, `kernel/`, `boot/` and `tests/` turns up one subject
and one only:

- **#343, `974c976d`, 2026-11-06**: *"Fix Pravetz boot freeze after keyboard
  BIOS probe"*. The Pravetz 16S / CPU12 Plus ROM answers `int 15h` AH=C0h with
  `STC / MOV AH,86h / RETF 2`, which leaves IF clear. After #313's probe (merged
  2026-10-26) the serial-mouse reset then waited for ever at `ticks = 2`
  (SPEC.md 9.7.2, `SPEC.md:15942`; `kernel/mouse.inc:681`). So between
  2026-10-26 and 2026-11-06 that machine could not boot at all.
- `tests/pravetzboot.py` (`soak -k pravetzboot`) boots "the real ROM on 86Box's
  Pravetz, V20/12 MHz, 8087 and OTI-037C". Its suite row says *"86Box models
  the reported machine and BIOS"* (`tests/suite.py:8086-8097`). It boots
  **`build/os8088.img`, the 1.44MB disk**, on `fdc_at` with two `35_2hd`
  drives (`tests/pravetzboot.py:35-46`). It reads neither `boot_cylrun` nor
  `b2_cylok`, so it has never asked how this ROM handles a cylinder run.
- FIELD-NOTES 31's BIOS survey (`docs/FIELD-NOTES.md:645-660`) does not include
  the Pravetz. Its only head-crossing failures are two 286 clones: MR BIOS
  answers `CF = 0` for the whole request and transfers the first half, and the
  Packard Bell answers status `04` twice "then lies" (note 36,
  `docs/FIELD-NOTES.md:899-904`). Every XT-class ROM tried crosses correctly.

So the "can't use floppy cyl reads" finding exists only in the owner's
knowledge. It is worth a FIELD-NOTES entry. The entry should record which disk
the machine booted, which kernel, and the Floppy page's Reads verdict.
**Which disk it was is not recorded anywhere.** The only hint is the row's
configuration (3.5" HD drives, the 1.44MB image). That points at 1.44MB, but
it is an inference.

A V20 takes the 8086 arm of the `push sp` gate (`boot/boot2.asm:308-316`). So
on this machine stage 2 always attempts the cylinder bound, and the question
below applies to it in full.

## 2. What "can't use cylinder reads" can mean, and what each costs

Stage 2 has two separate rescues, and they catch two different BIOS behaviours:

| the BIOS, on a run that crosses a head | what catches it | where |
|---|---|---|
| **ERRORS** (CF = 1) | `read_run`'s retry. It makes 3 attempts with a controller reset between them, then `jne boot2_entry.rerun`, which drops `[b2_runmax]` to SPT and reloads the whole kernel at the track bound | `boot/boot2.asm:770-812`, `.rerun` 531-535 |
| **LIES** (CF = 0, short or wrong data) | only the canary: after the whole load, sum the 256 words at `KSIG_OFF`; on a mismatch `jne .rerun` | `boot/boot2.asm:425-473` |

`read_run` trusts CF = 0 for the whole run (`boot/boot2.asm:870`, "CF=0 is the
contract"). So a liar is invisible to everything except the canary. **The error
path does not depend on the canary at all.** It works on every geometry today,
whatever `KSIG_OFF` is.

Both rescues end the same way: one more full load at the track bound.
`boot_cylrun` stays 0, `b2_cylok` stays FE, and the session runs at the track
bound (`disk.inc:5354`). That is exactly "boots fine, but longer". SPEC.md
18.93.1 prices the reload at about 2.2 s on the 5150's 360KB disk. On 1.44MB the
reload is about 13 track calls against 7 cylinder calls (SPEC.md 18.93.2's
table), plus three failed attempts on the error path. PREDICTED, not measured
on this machine.

**The kernel cannot tell the two kinds apart afterwards.** Both leave
`b2_cylok` = FE (192, 472) and `boot_cylrun` = 0. BOOTDIAG prints a status only
after the track-bound pass has also failed (the print follows the `.rerun`
jump, 808-812). And **`rdiag360.img`, FIELD-NOTES 31's survey instrument, no
longer tests cylinder runs.** It is a `FLAT_PAYLOAD` read by stage 1, and stage
1's loop is now track-bounded (`boot/boot.asm:374-404`, `mov ax, SPT ... the run,
bounded by the track`). Its header still says *"boot.asm partitions it into
exactly the same transfer runs"* (`tests/rdiag.asm:6-8`;
`docs/MARTYPC-DEBUG.md:1302` recommends it for exactly this question). On any
ROM it now draws all dots. That is a second inert instrument beside the canary,
and it needs a line in the register.

## 3. When the canary went inert

The rule that matters (SPEC.md 18.93.1) is about the **run** that contains the
canary sector: that sector must sit after a head flip inside a run that began
before the flip. Two things decide where runs start:

- `read_run` cuts at multiples of `[b2_runmax]` (the cylinder).
- Under KZIP it also restarts at two places: `.text` (stage 2's first run,
  `[b2_lba0] + BOOT2_SECS`, 324-328) and `.text + KZ_HEADSEC`, because the plain
  head is read in runs of its own (`boot/boot2.asm:341-345`) and the tail starts
  a fresh run after it (353-357).

`KZ_HEADSEC` has been 9 (`SPL_RESIDENT`, `kernel/splash.inc`) since KZIP landed.

**On a 9-sector, 2-head disk the canary cannot be live under KZIP, at any blob
length.** File sector 21 is LBA 33 when KERNEL.SYS starts at LBA 12, and LBA 35
when it starts at LBA 14. Both lie in LBA 27..35, the **head-1 half of cylinder
1**. A run that contains either sector crosses a flip only if it began in
18..26. But under KZIP the canary's run begins either at `.text` = KERNEL.SYS +
blob (canary in the head; blob ≥ 13, so ≥ 27) or at `.text + 9` (canary in the
tail; ≥ 12+8+9 = 29). Neither start is ever ≤ 26.

MODELLED over the blob lengths and KERNEL.SYS positions this tree has shipped:

| date (`elendilon` commit; main squash) | event | blob | 360KB | 720KB | 1.2MB | 1.44MB | kern_small 360/720 |
|---|---|---|---|---|---|---|---|
| before 2026-09-05 | plain load | 8 | live | live | live | live | live |
| **2026-09-05 `175926d5`** (main `2ce1e37b` #172, 09-08) | **KZIP the default** (`Makefile`, `ifeq ($(NOKZIP),) KZIP := 1`), head read in its own runs | 8..11 | **INERT** | **INERT** | **INERT** | live | **INERT** |
| 2026-10-17 `5c39aaf9` | 360KB system disk takes 512-byte clusters, so KERNEL.SYS moves from LBA 12 to 14 | - | INERT | INERT | INERT | live | INERT |
| 2026-10-17 `0efc7158` (pass 13) | blob 12 | 12 | INERT | INERT | INERT | **INERT** | INERT |
| 2026-10-24 `a30df75f` (pass 14; main `a9611731` #313, 10-26) | blob 13, `KSIG_OFF` 4608 to 4096 | 13 | INERT | INERT | **live again** | **live again** | INERT |

So:

- **On the shipped (KZIP) kernel the 360KB and 720KB canary has been inert for
  two months**, since compression became the default. SPEC.md 2.9.13.3, *"The
  canary is unmoved, and its reasoning is untouched"*, written in that same
  commit, is the error. It checked that file sector 21 is past a flip. It did
  not check that the head split had moved where the canary's run starts.
- **The 12 -> 13 step is not what broke it.** It broke only the `NOKZIP=1` arm,
  which ships nowhere. That is the area report's model (`areas/boot.md:135-137`),
  and it assumed a plain load. On the shipped kernel the same step **re-armed**
  the 1.2MB and 1.44MB disks: 1.44MB had been inert for that one week of the
  12-sector blob, and 1.2MB since KZIP.
- `t_canary` never saw any of this. `crosses()` walks cylinder-aligned runs from
  the **data area** (`tests/unit/t_canary.py:99-117`, `lba = data` at 110). It
  models neither the blob nor the head split, so it has answered "crosses" for
  file sector 21 the whole time.
- kern_mega is not built in `build/` (no `megak/*.img`). With its 14-sector blob
  and `KSIG_OFF` 3584 the canary is in the head, at the inert LBA 35 on 9-sector
  disks and live on 1.2MB/1.44MB. MODELLED, same argument.

**Today's six shipped system images**, MODELLED off their own BPBs
(KERNEL.SYS at LBA 14/14/29/33/12/12):

| image | canary LBA | its run | crosses? | splash before canary safe? |
|---|---|---|---|---|
| `os8088-360.img` | 35 | 27..35 (head 1 only) | **no** | yes: the head is one track |
| `os8088-720.img` | 35 | 27..35 | **no** | yes |
| `os8088-120.img` | 50 | 42..50 (flip at 45) | yes | **no**: `.text` sectors 3..8 land past the flip and the first tick runs `vid_setmode` at `.text` 0x7F5 |
| `os8088.img` | 54 | 46..54 (flip at 54) | yes | yes: only sector 8, the canary itself, is past the flip |
| `small360.img`, `small720.img` | 33 | 32..35 (head 1 only) | **no** | **no**: head 23..31 flips at 27 |

That agrees with BR-1 and its fit lens: **only kern_big 1.44MB is protected end
to end.**

## 4. Is the field report consistent with this?

Yes, with every reading that fits the words "boots fine":

- **An erroring ROM, any disk.** The retry path rescues it and does not need the
  canary. It boots fine and longer. Today's inertness is irrelevant to it.
- **A lying ROM on the 1.44MB disk**, the disk the row's configuration suggests.
  The canary there is live and the head's first eight sectors are correct, so
  the splash is safe. The canary fails, the kernel reloads at the track bound,
  and the machine boots fine and longer. Again, the canary is doing its job.
- **A lying ROM on a 360KB or 720KB disk would NOT boot fine today.** Here is
  the sequence, PLAUSIBLE and not run:
  1. The head (27..35) is correct.
  2. Every crossing tail run (36..53, 54..71, ...) brings back its second half
     wrong.
  3. The canary at LBA 35 passes.
  4. `kz_all` expands the corrupt stream. The decoder is unbounded by decision
     (SPEC.md 2.9.13.2), so it most likely hangs or writes somewhere it should
     not, possibly over stage 2.
  5. If a kernel does start, it is scrambled. Line 473 has also published
     `boot_cylrun` ≠ 0, so `dsk_cylrun` = 1 and every crossing READ of the
     session returns wrong data (`disk.inc:5354`).

  So this report rules that combination out for this machine. It is no evidence
  that the canary works on those disks, and none that it was broken lately.

**A correction to BR-1's blast radius.** BR-1 says *"every multi-sector
**write** of the session crosses heads"*. Cylinder runs are reads only (SPEC.md
18.91.3). The real harm is a crossing READ that returns the other head's
sectors, plus a FAT or directory sector read wrong and then rewritten (SPEC.md
31.14). The severity is the same, but it is the reads that go wrong.

**Who is exposed today.** On an 8088/V20, a lying ROM booting a 360KB or 720KB
disk, on kern_big or kern_small. No such XT-class ROM has been found (FIELD-NOTES
31), but 360KB is the 8088's disk. On a 286, the gate keeps the track bound
unless Cylinder is ticked. Even then, `cpc_fdtest` tests the pick at the save
and turns a lying ROM's pick back to Auto (SPEC.md 31.14; `tests/fddpage.py`).
So the 286 path has a second fence, and the 8088 path has none.

## 5. The fix, confirmed and re-priced

**(1) Move the canary, and drop the segment arm.** File sector 57 is right:
`KSIG_OFF := 22528` on kern_big (the Makefile's `-512`/`+1024` arms,
`Makefile:2242/2252`, carry it to every other arm; kern_small becomes 23552,
which is also file sector 57). MODELLED with the head both track- and
cylinder-bound, it lands at LBAs **71/71/86/90/69/69**. Each sits after the flip
of its run on all six images, the same figures the truth lens found.

**The +11 bytes BR-1 charges for this are avoidable, and the change can save
bytes.** The `%if KSIG_OFF >= KZ_HEADSEC * 512` arm (`boot/boot2.asm:437-462`,
466-468) bumps ES by `KZ_RPARA` only to undo the tail's lift. Its own comment
says the two adjustments cancel, so the address is `KERNEL_SEG : KSIG_OFF +
KZ_RPARA*16`. That is inside the 64KB the handoff's ES already names on all
three kernels:

| kernel | KSIG_OFF + R×16 |
|---|---|
| kern_big | 22528 + 18432 = 40960 |
| kern_small | 23552 + 10240 = 33792 |
| kern_mega | 22016 + 24064 = 46080 |

(R from each `kernel.kz.json`.) So the change is:

- `mov si, KSIG_OFF + KZ_RPARA*16` under KZIP, which is the same 3-byte
  `mov si, imm16`;
- an assemble-time `%error` if that sum plus 512 passes 65536.

That costs **0 bytes on kern_big and kern_mega and saves 11 on kern_small**,
whose 5120 assembles the arm today. kern_big's 4 free blob bytes do not limit
it. The first pass assembles with `KZ_RPARA=0` (`Makefile:2839`), which changes
the immediate's value but not its length.

**(2) Keep the head at the track bound.** This is for the splash, and it matters
on 1.2MB and both kern_small disks. The cost lens's safe form stands: `mov
ax,[b2_spt] / xchg ax,[b2_runmax] / push ax` before the head loop and `pop word
[b2_runmax]` after it. That is +12 `.boot2`, and it keeps `.rerun`'s one-shot
property. kern_big has 4 bytes free in the blob, so this half still waits on
boot-7's -41 (or a 14th sector). kern_small (702 free) and kern_mega (285) can
take it now. Price: one more `int 13h` per 8088 boot on small360/720, 1.2MB and
1.44MB, and none on kern_big 360/720. PREDICTED, as BR-1 says.

**(3) Rewrite `t_canary.crosses()`, and test it.** It must model what the loader
really does:

- start at KERNEL.SYS + `BOOT2_SECS`;
- read `KZ_HEADSEC` (from `kernel.kz.json`) in its own runs;
- restart the tail there;
- apply the DMA-page bound.

Break it on purpose: `KSIG_OFF` back at 4096 must turn `os8088-360.img`,
`os8088-720.img`, `small360.img` and `small720.img` red. boot-10's
"independent reader" point stands, because a gate that imports the generator's
model agrees with it by construction.

**(4) Two record fixes that go with it.**

- **SPEC.md 2.9.13.3**: replace "the canary is unmoved" with the run-start rule.
- **rdiag**: either give stage 1's flat arm the cylinder bound behind a define,
  or retire the instrument. Fix `tests/rdiag.asm:6-8` and
  `docs/MARTYPC-DEBUG.md:1302` in the same change.

**Urgency: land (1) and (3) now.** They cost zero bytes on the shipped kernel
(-11 on kern_small), they re-arm the canary on the four disks an 8088 actually
boots, and nothing waits on room. (2) follows boot-7. BR-1's ranking stays high
severity, low likelihood: no silent XT-class ROM has been seen, but none would
be survivable on a 360KB disk today.

**To settle the Pravetz itself**, use one run of the existing row, extended
(soak, 86Box, about 8 s). Read `boot_cylrun` and `b2_cylok` at the desktop, and
count the boot's `int 13h` calls or catch `.rerun`. Do it on `os8088.img`, and
once more with a 360KB image on a `525_2dd` drive. If `.rerun` is entered from
`read_run`, the ROM errors. If it is entered from the canary, the ROM lies. This
holds only if 86Box's model of this ROM matches the field machine, and the row
says it does. Then ask the reporter which disk they booted.

### The model (for the record)

```python
# run partition stage 2 makes under KZIP (boot2.asm 324-357, read_run 734-760)
def runs(lba0, blob, ksecs, spt, heads, head=9, rpara=1152):
    rm, out, dest = spt * heads, [], 0x60
    def load(lba, dest, left):
        while left:
            n = min(rm - lba % rm, left); pg = (-(dest * 16)) & 0xFFFF
            if pg: n = min(n, pg >> 9)
            out.append((lba, n)); lba += n; left -= n; dest += n * 32
        return lba, dest
    lba, dest = load(lba0 + blob, dest, head)      # the plain head, own runs
    load(lba, dest + rpara, ksecs - head)          # the tail, a fresh run
    return out
# live  <=>  the canary's run starts before a track flip and the canary is at/after it
```
