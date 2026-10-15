# Load `HDD.DRV` at boot when the machine has a hard disk — and make the blob one section first

**BUILT, builds 903-907, on `autoload-hdd`.** SPEC.md 2.9.14, 2.9.14.1,
51.3.2 and 51.5.4 are the contract now; this file is the design record and
the plan as it was written, kept with the measurements that replaced its
estimates. The three parts landed as five commits:

| commit | what | measured |
|---|---|---|
| A1 (903) | the blob is one section: `.ovl` merged into `.boot2` | blob 4,751 of 5,120, **369 spare** (was 10); `KERN_SIZE` unchanged; `SPLSTARS=1` no longer needs `NOKZIP=1` |
| A3 (904) | `t_blobruns` ratchets stage 2's read too | 11/11/8/7 calls at ten sectors, checked against MartyPC's FDC; an 11th blob sector is free, a 12th costs 720KB a call |
| A2 (905) | the window half is `.ovl` again; `OVWCALL` is `OVLCALL` | byte-identical images |
| B (906) | the `DA` key | `.bss` +2 resident, blob +55, `CTRL.DRV` +25 |
| C (907) | the sniff, `DRVE_NOVOL`, the unload | window half +30 (15 of 5,120 left), blob +31, `CTRL.DRV` +19, `HDD.DRV` image unchanged at 3,584 |

What the estimates got wrong, so it is not re-derived:

- **A1 cost no byte at all**, not three: `splash.inc` already sat ahead of
  the loader, so offset 0 was already a `jmp boot2_entry`. It also found a
  real trap — a `.text` word naming the blob's END (`kbld_ovl`) makes the
  compressed kernel's two passes pack different bytes once the loader is the
  last thing in the section, and SPEC.md 2.9.13.4's check refused the build.
- **Stage 2's read is 11 calls, not the 9 the cylinder arithmetic in A.5
  gave**: the plain head is a call of its own and the DMA pages one more. The
  conclusion stood (the 11th sector free, the 12th not).
- **The autoload costs +969 ms of `drv_boot_x`** on the 4.77 MHz XT with an
  XT-IDE ROM (2,342 against 1,373 ms answered-off; 9 more floppy sectors in 2
  more reads), not C.5's 0.2-0.4 s: the estimate priced the read and left
  out the expand, attach, the C: mount and the quiet remount of A:. The sniff
  itself is 3.4 ms on a disk-less machine, OPL2 probe included.
- **The `WANT=2` marker was never built**: `DRVR_ANS` answers the same
  question and the boot loop asks it.
- **The owner chose B.2's first arm**: an old file's OFF rows are unanswered.

`tests/hdauto.py` is the gate (six legs, red with the `DRVR_ANS` mask or the
unload taken out); `tests/hdoff.py` is the fixture that starts the rows which
tick the hard disk in themselves from the state a user reaches by unticking.

---

# Part A — one blob, and `.ovl` means the window again

## A.1 What the split is today

The blob is `BOOT2_SECS` × 512 = **5,120 bytes**. Stage 1 reads it, stage 2
copies it to `HEAP_SEG`, `mem_unblob` frees it at `spl_finish`, and
`BLOBSUM` checks all 5,120 bytes. It holds two NASM sections:

| | file offset | vstart | used | share | idle |
|---|---|---|---:|---:|---:|
| `.boot2` (loader, splash, decoder) | 0 | 0 | 2,265 | `OVL_AT` = 2,624 | **359** |
| `.ovl` (blob half of the overlay) | `OVL_AT` | `OVL_AT` | 2,486 | 2,496 | **10** |

(`kern_emu`'s `.ovl` is 2,492, so it has 4 idle. `kern_small`'s is 2,076.)

**They are already one segment.** `.ovl`'s vstart is its file offset, so an
overlay label is a blob offset, reached through the same `[spl_fseg]` as the
splash. Both have the same lifetime, the same read and the same checksum. The
only thing that separates them is `OVL_AT`, a constant set by hand. That
constant's floor is set by a knob build that ships nowhere: `SPLSTARS=1`'s
uncompressed loader is 2,568 bytes, and `OVL_KNOBGIVE` (96/128) and
`OVL_BASE` exist so that knob loaders up to ~2,500 bytes still assemble. **So
the shipped build's layout is dictated by its knobs.** Every byte added to the
overlay hits a 10-byte wall while 359 bytes sit idle below it.

The calling conventions are already one convention under two names. These
pairs have identical bodies in `kernel/kernel.asm`:

- `OVLCALL` and `SPLCALL`;
- `OVLGATE` and `SPLGATE`;
- `OVLGATE1` and `SPLGATE1`.

## A.2 The merge

Every `section .ovl` (25 fragments in 12 files) becomes `section .boot2`.
There are two ways to keep stage 2's entry at offset 0:

- **(a) One section, recommended.** `boot/boot2.asm` is included at the FOOT
  of `kernel.asm`, because `B2_KSECS` is derived from `MODC_START`, so the
  former overlay fragments land ahead of the loader in source order. A
  `jmp near b2_entry` at offset 0, emitted where `.boot2` is declared, keeps
  stage 1's `HEAP_SEG:0000` entry. **Three bytes of blob are the whole cost.**
- **(b) Zero bytes.** Declare the old fragments as a separate section with
  `follows=.boot2 vfollows=.boot2 align=1 valign=1`; the tree already lays
  out `.modu` this way. That also removes the hand-set constant, but it keeps
  two names for one thing, which is what this part exists to stop.

What it deletes:

- `OVL_AT`, `OVL_KNOBGIVE` and `OVL_BASE`;
- the two guards (`BOOT2_SIZE > OVL_BASE`, `OVL_BASE + OVL_SIZE >
  BOOT2_PAD`), replaced by **one**: the blob's end ≤ `BOOT2_PAD`;
- guard 4b (an empty overlay can no longer exist as a separate thing);
- `OVLCALL`, `OVLCALLC`, `OVLGATE` and `OVLGATE1`. Their sites, about 58,
  become `SPLCALL`, `SPLGATE` and `SPLGATE1`, plus an `SPLCALLC` for
  `OVLCALLC`'s carry-keeping form;
- `tools/os88ladder.py`'s `OVL_AT` row.

**What it buys, measured on today's sizes:**

| build | blob idle today | after the merge |
|---|---:|---:|
| `kern_big` | 10 (+359 locked below the split) | **369** |
| `kern_emu` | 4 | **363** |
| `kern_small` | 2,076 of 2,496 used | about 780, if its loader matches big's 2,265 |
| `SPLSTARS=1` (loader 2,568) | n/a | 66, and it fits |
| knob builds (11 sectors, `BOOT2_KNOBSEC`) | `OVL_KNOBGIVE` juggling | a 5,632-byte pool. `BOOTMARK=1 DRVDIAG=1`, which `kernel.asm` currently lists as "given up", fits |

**Resident: zero.** `.text`'s call sites into the blob are
`mov word [spl_fp], label`, so the merge changes their immediates and not
their size. `KERN_SIZE` and `KERN_BUDGET` do not move. The image is no longer
byte-identical, so verification is by boot (A.6). "Cleanliness, not bytes" is
modest: it is 359 bytes of the one budget that binds boot-only code, at no
disk cost and no RAM cost.

## A.3 The rename: `.ovlw` → `.ovl`

Agreed. The window half is what the name meant when `.ovl` was invented. Four
things come with it.

1. **Two commits, in this order.** First the merge, after which the name
   `.ovl` exists nowhere in the code. Then the rename. Any `.ovl` in code
   after the second commit is the window, and `git log -S` on either side of
   the first commit is unambiguous. Doing both in one commit is a swap, and a
   swap is the shape that gets misread.
2. **The code is guarded and the prose is not.**
   - Code: `tools/os88ovlchk.py`'s rule 2d (the call macro must match the
     target's half) fails a stale branch that calls with the old names.
   - Code: a stale `section .ovl` body meaning "blob" that lands in the
     window after a merge is caught by rule 2e if anything calls it after the
     mount. If nothing does, the window was a correct home for it anyway.
   - Prose: SPEC.md mentions `` `.ovl` `` 93 times and `.ovlw` 74 times.
     CLAUDE.md does 5 and 3, `docs/KERNEL-MEMORY.md` 8 and 8, and 45 `.md`
     files name `.ovlw`. The live documents get swept.
     `docs/plans/completed/`, `docs/reports/` and commit messages are records
     and must not be edited, so SPEC.md 2.5.3 carries a dated line: *"Before
     build N, `.ovl` named the blob half and `.ovlw` this one."*
3. **The macros rename with it.** `OVWCALL` (45 sites) becomes `OVLCALL`,
   which is free after A.2. `OVBCALL` (24) keeps its name, and its two arms
   become `.ovl` and `.boot2`. The `kern_small` spellings
   (`%ifndef KERN_SMALL section .ovlw %else section .ovl`, SPEC.md 2.5.3.2)
   become `.ovl` and `.boot2`.
4. **The tools rename in the same commit, or they lie.**
   - `tools/kernsize.py`'s `SECTIONS` and `MOD_SECTIONS`, plus the blessed
     baseline in `docs/KERNEL-MEMORY.md`. Unblessed, the report reads
     `.ovl +2,589` for a change that moved nothing.
   - `tools/os88ovlchk.py`: the section names and every rule's wording.
   - `tools/os88sym.py`, `tools/os88layout.py`, `tools/os88modcost.py` and
     `tools/os88ladder.py`.
   - `tests/ovlrefs.txt`, the registry, which is keyed by half.
   - `tests/dispcold.py`, `tests/mouresume.py`, `tests/unit/t_lowwin.py`,
     `tests/bootstatus.py`, `tests/unit/t_buildmatrix.py` and
     `tests/unit/t_pkgzrules.py`.
   - The Makefile's comments.

## A.4 Which half a body goes in

After the merge there are two homes and the rule is short:

| | window `.ovl` | blob `.boot2` |
|---|---|---|
| dies at | the first mount | `spl_finish` |
| room | 5,120 on big (45 spare), 1,536 on small (56 spare) | 5,120 (369 spare on big) |
| read | inside the COMPRESSED kernel read (~85%), under the progress bar | uncompressed, by stage 1, before the first splash pixel |
| entry from `kmain_o` (itself in the blob) | `call FAT_SEG:x`, 5 bytes | `BLOBCALL` (`push cs` / `call`), 4 bytes |

**A body that is dead before the first mount goes in the window, as long as
it fits.** The window is the cheaper byte on disk and is read where the user
can see progress. The blob is for the loader, the splash, anything that must
outlive the mount, and overflow from the window. Nothing has to move for
Parts B and C, and nothing should be moved for its own sake. A body moves when
a half is short, in the commit that needs the room.

## A.5 One more sector, but only if it costs no `int 13h` call

**Stage 1's read** of the blob is two calls on all four geometries up to
**13** sectors (`python3 tests/unit/t_blobruns.py --sectors 13`). The 14th
costs 720KB a third call.

**Stage 2's read is the tighter one.** A longer blob moves every kernel sector
after it, and that read is bounded at the cylinder (SPEC.md 18.91.1). With
the compressed kernel at 155 sectors, the kernel's last sector on each
geometry sits here against the next cylinder boundary:

| geometry | `KERNEL.SYS` LBA | kernel ends | next cylinder | room |
|---|---:|---:|---:|---:|
| 360KB | 12 | 176 | 180 | 3 sectors |
| 720KB | 14 | **178** | **180** | **1 sector** |
| 1.2MB | 29 | 193 | 210 | 16 |
| 1.44MB | 33 | 197 | 216 | 18 |

**So one more blob sector is free today on every geometry, and a second costs
720KB a tenth call.** This is my arithmetic: cylinder bounds only, not the
64KB DMA splits, which fall at fixed memory addresses and do not move with
the file. The same spare sector is also what any kernel growth uses up next.
**Nothing in the suite counts stage 2's calls**, so before spending the
sector, `t_blobruns.py` should walk the kernel's runs too and ratchet them per
geometry, the way it already does the blob's. Parts B and C do not need it:
369 bytes of blob after A.2 covers them.

## A.6 How A is verified

The merge and the rename change no behaviour, so the gates are about layout:

- `make` on all three kernels, plus `soak -k buildmatrix` (every knob
  kernel, `SPLSTARS=1` and the `BOOTMARK=1` pairings especially);
- `os88ovlchk`, `t_blobruns`, `t_canary` (`KSIG_OFF` does not move, because
  `BOOT2_SECS` does not), `kzboot` and `os88ladder.py --selfcheck`;
- **`full`**, because this reaches the integration branch as major work: a
  desktop on both 1bpp adapters and VGA, and `kern_small` on its 128KB
  machine;
- the splash and boot rows (`bootstatus`, `dispcold`, `mouresume`) and
  `hdboot`, because the hard-disk loader reads the blob too;
- `KERN_SIZE` identical before and after, on both kernels.

---

# Part B — drivers the user has answered (`DA`)

## B.1 What goes wrong without it

The sniffs (sound's, and Part C's hard disk) set a DEFAULT, and a
`SYSTEM.CFG` that deserialises overwrites every `DRVR_WANT`
(SPEC.md 51.3.1). The file cannot tell "the user unticked this" from "nobody
touched it", so **a Control Panel closed on a machine with no card or disk
saves that row as "off"**. A floppy carried to a machine that has the
hardware then never loads its driver. The owner's two conditions (section 7,
decisions 1 and 4) are that this must not happen, and that nothing detected
is ever switched off for good.

## B.2 The design

- **A new key, `DA`, at version 1: one word**, with one bit per row in
  `drv_cfgbit`'s order like `DW`. A set bit means the user answered this row.
- **The per-row runtime flag is `DRVR_PAD`, renamed `DRVR_ANS`.** Byte 15 of
  every `drv_tab` row is currently unused. The two standalone rows
  (`blank.inc`, `xmem.inc`) omit that byte, and nothing that walks
  `drv_tab` reaches them.
- **Load.** The sniffs run first, as now. `want_set` writes `DRVR_WANT` only
  for rows whose `DRVR_ANS` is set. An unanswered row keeps the sniff's
  answer, or 0 if it has no sniff.
- **A file with no `DA` key (every file in existence today) gets
  `DA := the ON bits of DW`.** Every explicit "on" stays answered and stays
  on. Every "off" becomes unanswered, so the sniff decides it. That is
  *better* than "existing as off is fine": existing machines with a disk get
  the autoload. The one visible change is that a sound card someone
  deliberately unticked comes back once. Unticking it again answers it for
  good. The alternative is "no `DA` key means everything is answered", which
  keeps every old "off" exactly and gives existing machines no autoload.
- **Save.** `DW` holds `DRVR_WANT` as now, and `DA` holds the `DRVR_ANS`
  bits. A panel closed on a machine with no disk writes the hard-disk row
  *unanswered*, so the next machine still sniffs it.
- **Click.** Ticking or unticking a row on the Drivers page sets `DRVR_ANS`.
  An answer is sticky. Putting a row back on automatic would need a third
  state on the page, and that is out of scope here.
- **A file read by an older kernel** skips `DA` by its length (SPEC.md 51.5
  rule 1) and uses `DW` as before.

## B.3 Cost (estimates)

| | bytes | resident? |
|---|---:|---|
| `drv_cfg` grows by a word | **2** | yes. SPEC.md 51.5's struct, exactly SETTINGS-COST's 2 per setting |
| boot parser: key-table entry, the `want_set` mask, the no-`DA` fixup | ~25-35 | no, in the blob |
| `CTRL.DRV`: key-table entry, the click's store, the pack | ~20-30 | on-demand module |

---

# Part C — the hard-disk sniff, and unloading on nothing to mount

## C.1 The sniff: one BIOS call, in the window

Extend `drv_snd_sniff` (renamed `drv_sniff`) with `hd_at_bios`'s own test,
so the sniff and attach cannot disagree about whether there is a disk:

```
    push es / push di           ; some BIOSes write ES:DI on AH=08h
    mov ah, 08h / mov dl, 80h / int 13h
    pop di / pop es
    jc .nohd
    test dl, dl / jz .nohd      ; DL = the count of fixed disks
    and cl, 3Fh / jz .nohd      ; no sectors = a BIOS saying no without CF
    inc dh / jz .nohd           ; heads 0FFh, the case hd_at_bios refuses
    mov byte [drv_tab + DRVR_SIZE + DRVR_WANT], 1   ; row 1 = the hard disk
.nohd:
```

About **30 bytes of the window half**. It is 5,075 of 5,120 on `kern_big`
and `kern_emu`, so 15 would be left. `SNDSNIFF=sb` is a knob build and gets
`DSK_OVLPAD`'s extra 1,024.

- **Only rung 0 is sniffed, deliberately.** SPEC.md 52 refuses to probe
  1F0h on every boot, and that stands. A 286 or later whose BIOS does not
  know its drive ticks the box as today.
- **The BDA byte at 0040:0075 is rejected.** It is not what attach tests, and
  whether a 1981-82 POST leaves it zero has not been checked.
- **Sound's sniff needs no change for Part B**: being unanswered is what
  marks a row as the machine's choice now. So the `DRVR_WANT = 2` marker in
  the first draft is gone.

## C.2 The driver says "nothing to mount": `DRVE_NOVOL`

- `hd_cfg_automount` returns `CF=1, AL=DRVE_NOVOL` when none of its
  `hd_vols` rows is live after the loop. That is about **12 bytes** in the
  resident, which ends at `0BC7h` and is padded to `0C00h`, leaving 57 bytes
  of zeros. **The image stays 3,584 and the claim stays 4 KB.**
- `drv_attach` already writes a `DRVV_READY` refusal into `DRVR_ERR`. When
  the user ticked the row, the driver stays loaded so the partitioner and
  formatter are reachable, and the row reads `Nothing to mount` instead of
  `Loaded`.
- `DRVE_NOVOL` = 8, `DRVE_NERR` becomes 9, and it is mirrored in
  `drivers/os88drv.inc` (`t_mirror`).
- The string goes in `drv_errstr`, which is in `CTRL.DRV`'s image: about
  **19 bytes, on-demand**.

## C.3 The boot loop acts on it: `drv_boot_x`, now in the blob

After each `drvf_drv_load` (BX = the row, CL = its index):

```
    call COLD_SEG:drvf_drv_load   ; existing
    jnc .ok
    cmp byte [bx+DRVR_ANS], 0     ; the MACHINE chose this and it failed:
    je .drop                      ; drop it and say nothing
    mov byte [drv_nerr], 1        ; existing - a choice the USER made failed
    jmp short .skip
.ok:
    cmp byte [bx+DRVR_ANS], 0
    jne .skip                     ; the user's choice stays up whatever it says
    cmp byte [bx+DRVR_ERR], DRVE_NOVOL
    jne .skip
    mov al, cl
    call COLD_SEG:drvf_drv_unload
.drop:
    mov byte [bx+DRVR_WANT], 0    ; in RAM only: DA says unanswered, so the
.skip:                            ; next boot detects again (decision 4)
```

About **30 bytes of blob**, out of A.2's 369. It applies to every
machine-chosen row, so a sound card the sniff found but the driver refused no
longer opens the Control Panel at boot (decision 2).

## C.4 What the user sees

| machine | settings file | after |
|---|---|---|
| no hard disk | any | nothing. One `int 13h` that refuses at once, and the row is saved *unanswered* |
| hard disk with a FAT partition | none, old, or HDD unanswered | **C: on the first desktop** |
| blank, non-FAT or unpartitioned disk | HDD unanswered | loads, mounts nothing, unloads, the row reads `Nothing to mount`, no Control Panel. Detected again next boot |
| installed, booted from C:, one partition | HDD unanswered | the same; the kernel's C: is untouched |
| any | HDD answered off | nothing |
| any | HDD answered on | loaded, plus a `Nothing to mount` caption if that is true |

## C.5 Time, on the 4.77 MHz target

These are predictions; section 6 step 1 measures them.

- **No hard disk:** one `int 13h AH=08h, DL=80h`. A diskette-only IBM BIOS
  should reject drive 80h and function 08h as a bad command with CF=1,
  before touching the controller. **This must be confirmed on the 5150.**
  *Confirmed against the IBM 27 OCT 82 ROM itself under MartyPC: `AH=01h`,
  CF=1, row 1 untouched (SPEC.md 51.3.2).*
  A ROM that wrongly answers CF=0 is caught by the sanity tests and then by
  attach. The worst case is one wasted load, never a wrong mount.
- **A disk with something to mount:** what ticking costs today, paid on
  every boot:
  - the 3,136-byte `HDD.DRV`, 7 floppy sectors, about **0.2-0.4 s**;
  - an lz4 expand, about 30 ms;
  - the automount's handful of hard-disk calls;
  - **4 KB of heap** while the driver is up.
- **A disk with nothing to mount:** the same read and probe, about
  **0.3-0.5 s on every boot**, then the 4 KB is given back. Decision 4 accepts
  this as the price of detecting again every time.
- **A side effect:** the unload's detach still stages `hd_cfg_mark`, which
  sets `[cp_wdirty]`, so the next Restart rewrites `SYSTEM.CFG`. With
  Part B the contents say "unanswered", so it decides nothing. It is a floppy
  write the user did not cause. Skipping it would mean a "don't mark" path
  through detach, which is not worth bytes unless the write is noticed.

## C.6 The pre-check: priced, and refused

Deciding before the read whether anything could mount means:

- one hard-disk read per boot;
- a 512-byte buffer at MARK 25, while the window half is occupying the
  scratch;
- a second copy of `hd_part_used` and of the boot-partition exclusion;
- and it still cannot see a partition whose type says FAT but whose boot
  sector does not, so C.3 is needed anyway.

That is ~60-70 bytes of window against the 15 left after the sniff. It would
save ~0.4 s, only on machines with a disk and nothing to mount. **Not very
cheap, so it is refused.**

---

## 4. What else is awkward

### 4.1 Forty-odd test rows boot a machine with a hard disk

Every `*_hdd*` MartyPC profile carries `default_xtide.vhd`, and about 40
files under `tests/` boot one. Rows with no `SYSTEM.CFG`, or with one that
leaves the hard-disk row unanswered, will now load a driver they did not load
before. If the VHD is formatted they get an extra mount and 4 KB less heap.
If it is blank they get an extra floppy read on every boot. The rows at risk
are the ones that read:

- heap figures (`heapmap`, `heaphi`, `dosram`, `ems`, `sndmove`);
- boot timings;
- `disk()` counts.

The sweep is mechanical, and it goes in Part C's commit.

### 4.2 The stale plan beside this one

`docs/plans/HDD-RESIDENT-PLAN.md` still prices `HDD.DRV` at a 5,120-byte image
and a 5 KB claim (`d1ceac13`). Kernel size pass 11 took it to **3,584 / 4 KB**
(`bacea172`). It should carry a correction line, and nothing in it binds this
plan.

## 5. Cost, all three parts

| | resident kernel | boot overlay | `HDD.DRV` | `CTRL.DRV` |
|---|---:|---|---|---:|
| A: merge + rename | **0** | +3 (the entry jump), and 359 bytes of locked slack become usable | — | — |
| B: `DA` | **2** | ~25-35 blob | — | ~20-30 |
| C: sniff + unload | **0** | ~30 window, ~30 blob | ~12, inside existing padding; image unchanged | ~19 |
| **total** | **2** | blob 369 → ~280 spare, window 45 → ~15 | 3,584, 4 KB | ~45 |

No sector is added. If one is ever needed, A.5 says the first is free and
the second is not.

## 6. Order

1. **Measure** (MartyPC: `os8088_xt_hdd`, `os8088_5150_cga_hdd_gla`, and a
   disk-less GLaBIOS profile). Boot time ticked against unticked, on a
   formatted and on a blank copy of the VHD, plus `int 13h AH=08h DL=80h`'s
   answer with no disk. This turns C.5 into measurements.
2. **A, commit 1: the merge.** SPEC.md 2.5.3 and 2.9.6 first. Then
   `kernel.asm`, the 25 fragments, the four macros, `os88ladder.py`, and the
   comments that cite `OVL_AT`. Verify per A.6.
3. **A, commit 2: the rename**, with every tool, the registry and the
   `kernsize` baseline in the same commit (A.3 item 4). Verify per A.6.
4. **The stage-2 ratchet in `t_blobruns.py`** (A.5). It is host-side and
   cheap, and it is what makes "no extra call" checkable.
5. **SPEC.md for B and C:**
   - 51.5 gets the `DA` key;
   - a new 51.3.2, "the first boot asks about a hard disk too";
   - `DRVE_NOVOL` goes in 51.3's list;
   - 52's opening "not wanted by default" paragraph changes;
   - 52.6.1 gets READY's answer.
6. **B**: `DRVR_ANS`, `DA`, the masked `want_set`, the no-`DA` fixup, and the
   click.
7. **C**: `HDD.DRV`'s answer (assert 3,584), the sniff, the loop, the
   string. Check `kernsize` on `kern_big` and `kern_emu`. `kern_small` is
   untouched, because `OS88_DRIVERS` is `kern_big`'s (SPEC.md 51.0).
8. **A new soak row, `hdauto`** (MartyPC; docs/WRITING-TESTS.md). It reads
   `drv_tab` row 1, `dsk_vtab` and `[drv_nerr]` out of the guest:
   - formatted VHD, no `SYSTEM.CFG`: up and mounted;
   - blank VHD: down, `DRVE_NOVOL`, no notice;
   - no disk: no read of `HDD.DRV`;
   - answered off on a disk machine: not loaded;
   - answered on with a blank VHD: up, with `DRVE_NOVOL`;
   - a `SYSTEM.CFG` saved on a disk-less machine, then booted on a disk
     machine: **loaded** (decision 1's condition);
   - an old file with no `DA` and the bit off: loaded;
   - installed one-partition C:: down, and C: still lists.

   **Break it on purpose:** drop the `DRVR_ANS` mask in `want_set` and the
   disk-less-then-disk leg goes red. Drop the unload and the blank leg goes
   red.
9. **The 4.1 sweep**, in the same commit as C.
10. **Scoped re-runs:** `soak -k 'hd*' 'inst*' kdhdd knobhd hibernate
    hibernatedrv dossnd drvup 'cpnames*' heapmap heaphi dosram ems sndmove
    'vid*' buildmatrix`.

## 7. The owner's decisions (2026-10-13)

1. **Existing files that say "off": fine, as long as a Control Panel closed
   on a machine with no drive does not save "off" for the future.** That
   condition is what Part B is for. B.2 goes one step further, and that step
   is the owner's to accept or refuse: with no `DA` key, an old "off" is
   treated as unanswered rather than answered, so existing machines get the
   autoload too.
2. **The sound sniff (and `xm_sniff`) follow the same rule: yes.** Under
   Part B that comes free. Any unanswered row that fails is silent, so no
   `WANT=2` marker is needed. `xm_row` is not stored in the file at all: its
   sniff already decides every boot, so it needs no change.
3. **Funding the overlay: Part A, then ask again if more is needed.** After
   A.2 nothing more is needed (section 5).
4. **Do not switch off what was detected, because disks move around.** C.3
   sets `DRVR_WANT` to 0 in RAM only, and `DA` stays unanswered, so every
   boot detects again. The cost is C.5's re-probe on a disk with nothing to
   mount.
