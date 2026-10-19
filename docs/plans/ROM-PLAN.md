# Part of the system in ROM — what a 5150's sockets can hold, and what that buys

> **WAVES 0 TO 4 ARE BUILT - kern_small and kern_big both find their ROM,
> and kern_small's carries its daily modules and the Task Manager. SPEC.md
> 2.10 is the contract.**
> The rest of this file is the investigation they came from, kept as written.
> **Its section names are the OLD ones** (SPEC.md 2.9.14): where it says
> `.ovl` it means the blob's overlay half, which is part of `.boot2` now, and
> where it says `.ovlw` it means the window half, which is `.ovl` now.
>
> * **W0:** `.cold` names no segment of its own (`COLDCALL`/`COLDSEG_TO`), and
>   the blob sentinel is 0. Soak row `coldpic`.
> * **W1:** `make socketrom`, the per-socket files, MartyPC's per-instance
>   ROM (`os88marty.launch(rom=)`), and `tests/romsock.py`.
> * **W2:** `ROM_COLD` on kern_small, plus `make rom`. Measured under
>   MartyPC's GLaBIOS 5150 (`tests/romsmall.py`):
>   * the ROM is adopted;
>   * `.cold` runs from F401 (F400 since `.cold`'s offsets start at 0x10 -
>     docs/plans/HMA-PLAN.md, the same adapter loaded into the HMA);
>   * the floor falls by the cold rung - 22.0KB at build 234 - which is
>     **87.5KB of heap on a 128KB machine against 65.5**;
>   * modules are re-pointed on load;
>   * a ROM one byte away is refused.
>
>   It costs **+28 resident bytes** (`.text` +2, `.cold` +26). kern_big
>   assembles byte-identical.
>
> * **W3:** `ROM_COLD` on every kernel; `make rom` cuts both ROMs
>   (`rom-big`, `rom-small`). Measured on a CGA 5150 and on the VGA XT, both
>   GLaBIOS (`tests/rombig.py`):
>   * the floor falls **38.0KB** on both adapters at build 234: 1880 -> 0F00
>     on CGA, and 18A0 -> 0F20 on VGA, where the planar decoder's buffers move down into
>     the bottom of the dead cold rung;
>   * the decoder, driven by a dithered Paint canvas off the byte grid,
>     decodes every row on 0F00 with the ROM, on 1880 without, and draws
>     the same screen to the byte;
>   * a ROM one byte away is refused.
>
>   It cost **+47 resident bytes** on kern_big at build 364 (`.text` +4,
>   `.cold` +43), against `NO_ROM_COLD`, which assembles byte-identical to the
>   tree before it; SPEC.md 2.10.4 carries today's figure. The window had
>   **1,632 bytes** to spare then (3.6's ~2KB, measured).
>
> * **W4:** whatever room `.cold` and the tail leave holds module images,
>   greedily in `tools/os88rom.py`'s `PRIORITY` order, and on kern_small one
>   package. At build 364 kern_small's ROM holds FDLG, FILECP, CTRL, the small
>   Task Manager and FORMAT (4,055 bytes spare at build 234); kern_big's
>   held FORMAT (590) at build 387 and holds no module at build 234, its
>   `.cold` having grown past it (1,018 spare).
>   Measured on the 128KB machine with the system disk swapped out of A:
>   (`tests/romnodisk.py`): a Save As chooser, the Control Panel and the Task
>   Manager all open from the ROM, and all three refuse without it. +34
>   resident bytes of `.text` on kern_small (`ROM_PKG`, the Task Manager's
>   arm) and +35 of `.cold` on both (the module arm, in ROM when the ROM is
>   in). `$OS88_ROM` makes any emulator row a
>   with-ROM run (docs/MARTYPC-DEBUG.md).
>
> **What was built differs from section 3.4 and section 4 in eight places**,
> each found by building it:
> 1. **There is no key in the kernel.** A post-assembly key would break
>    `os88sym`'s re-assembly check, which every emulator row rests on. The
>    ROM verifies the kernel itself instead, more strongly:
>    * `.cold` byte for byte against the copy just expanded;
>    * a hash over `.text`'s CODE, skipping its data, which the boot writes
>      before the ROM is asked (41 bytes on the first build: the boot timer,
>      `[spl_fseg]`, every `vid_*`);
>    * every site checked before any is written.
> 2. **`.cold` is still read and expanded on a ROM machine.** Test 1 needs
>    it, so section 3.4.6's boot saving is the open follow-on - assessed
>    there: the identity is solvable, and `.boot2` has no room for it.
> 3. **The blob is lifted three sectors** (`BLOB_LIFT`). With `.cold` on top,
>    the packed tail's sector-rounded read reached stage 2 itself, which died
>    at 0000:0068. There is a guard for it now, on every build.
> 4. **`mod_need` hands the ROM the module's ROW, not its id.** `mod_check`
>    clobbers DI, and the first ROM was handed 8C58 as a module number.
> 5. **The doorbell is the boot overlay's WINDOW half, not stage 2's.**
>    `kmain_o`'s first instruction far-calls it in `.ovlw`. In `.boot2` it
>    cost four kern_small knob kernels their build, because a knob gives the
>    loader's slack to the overlay; in `.ovl` it did not fit kern_big, which
>    had 27 bytes to spare. It is 28 bytes now plus a 3-byte call (header
>    format 2, SPEC.md 2.10.1): rung by `dsk_boot_from_x` on its way out,
>    through a far pointer in the header, with `rom_patch` keeping every
>    register and finding the blob itself. Its first `.ovlw` shape was 46,
>    and the re-cut onto the next `elendilon` left kern_big's `.ovlw` 34
>    bytes - 12 short - while a 5-byte call from `kmain_o` broke the
>    `BOOTMARK=1` knob's `.ovl`, so nothing of it is in `.ovl` now.
> 6. **The ROM tool assembles with PASS 2's defines.** `.ovl` names a
>    stage-2 label through CS (`and al, [cs:b2_cylok]`), so on kern_big the
>    first-pass placeholders moved one byte of `.ovl` and the tool refused
>    the kernel. It reads `kernel.kz.json` now.
> 7. **Modules are COPIED out of the ROM, not run in place** (section 4.1
>    said in place). kern_small's modules keep their `.bss` in the claim's
>    tail and write it through CS since MODULE-SELFCONTAIN-PLAN, so an image
>    in ROM could not run there. The copy still buys what section 4.4 wanted
>    - no disk read and no system disk - and costs a claim, as a disk load
>    does. `FILECP`'s stack never had to move.
> 8. **The ROM's tables are per mod_tab ROW, not per image.** W3 cut them
>    per image, and kern_big's settings core (`MOD_SETS`, SPEC.md 2.8.7) is
>    a row over CTRL.DRV's image - so the first big ROM refused a desktop
>    shortcut gesture's load. Found by reading, fixed in W4, and covered by
>    running `tests/desksc.py` under `OS88_ROM`.
>
> The testbed is 5150 #2 (docs/FIELD-MACHINES.md): GLaBIOS on a One ROM, and
> two more One ROMs across U28–U32.

It was asked for on branch `rom-plan`, cut from `elendilon` at `351461a`
(build 358). The original Macintosh kept part of its system in a 64KB ROM. The
question is whether os8088 can do the same with the ROM sockets an IBM PC
already has, in two forms:

1. **The kernel.** Put ~32KB of kernel code in ROM so it stops costing resident
   RAM. Look at two shapes: a normal disk kernel that DETECTS the ROM and does
   not load that part, and a ROM-REQUIRED build if detection is too expensive.
   Answer this for both `kern_big` and `kern_small`.
2. **System applications.** Put the Task Manager, the Control Panel and two
   common drivers (sound, hard disk) in ROM. `kern_small` has no drivers, so
   it may get another application instead.

The measurements are in [docs/reports/ROM-COLD-2026-10-04.md](../reports/ROM-COLD-2026-10-04.md).
The instrument is `tools/os88romfix.py`, which re-derives every fixup and
section figure below in about nine seconds on any built tree.

---

## 0. The answer

| | `kern_big` | `kern_small` |
|---|---:|---:|
| `.cold` (kernel code with its own CS, SPEC.md 2.6) | **37,998** bytes, rung 38,400 | **24,383**, rung 24,576 |
| `KERN_SIZE` today → with `.cold` in ROM | 99,328 → **60,928** | 65,536 → **40,960** |
| heap on a 128KB machine (arithmetic) | — (boots on 196KB today) | 62.5 KB → **86.5 KB** (+38%) |
| fits the four BASIC sockets alone (32,768) | **no**, 5,230 over | **yes**, 8,385 spare |
| fits U28–U32 as one option ROM (~40,780 for `.cold`, §1.3) | **yes today**, ~2,780 spare, but over on 8 of the 21 days with an integration commit since 1 September (§3.6) | **yes**, ~16,400 spare: room for its three daily modules and the Task Manager too (§3.7) |
| far-pointer fixups if `.cold` changes segment | **257**, all clean | **212**, all clean |
| fixups inside `.cold` itself | 10 (8 far calls) | 2 |
| packed sectors a ROM machine need not read at boot | ~69 | ~45 |

Eight findings carry the plan.

1. **Only `.cold` can go into ROM, and it is ROM-clean today.**
   - `.text` cannot go. The near model makes `CS = DS = KERNEL_SEG` the
     contract for every task and every API cell (§2).
   - `.cold` already runs with a CS of its own. `tools/os88ovlchk.py` already
     refuses data in `.cold` and any segment taken from CS. A disassembly of
     both kernels finds **no byte of `.cold` written at runtime**, no ISR in it,
     and no vector pointing into it.
   - The one CS reference is on `kern_big`, `call [cs:fdlg_htab+bp]`
     (`kernel/fdlg.inc:647`). It READS a constant table, which a ROM serves
     perfectly well.
2. **The segment is the whole problem, and it is mechanical.**
   - `COLD_SEG` is an equate, so a different segment is a set of patched words.
   - Assembling each kernel with the segment at two values and diffing proves
     the set is complete: 257 words on `kern_big` and 212 on `kern_small`,
     **every one a plain 16-bit immediate that moved by exactly the delta**.
     There are zero non-linear diffs.
   - Of the ten inside `.cold`, eight are cold-to-cold far calls and two are
     sentinel stores. All of them can be rewritten not to name the segment, so
     one ROM image can sit at **any** segment (§3.2).
3. **There is one real defect, and every ROM design has to fix it first.**
   - Nine emitted copies on `kern_big` (seven on `kern_small`) test
     `cmp word [spl_fseg], COLD_SEG / jbe`.
   - That test means "the boot blob is gone" only because `.cold` sits
     *below* the heap. At a ROM segment such as F600, every blob segment
     compares below it, so the overlay and the splash would never be called.
   - The fix is `je` against the sentinel. It costs 0 bytes (§3.3).
4. **Detecting the ROM at runtime costs about 16–23 resident bytes**, all of
   it `.cold` (§3.4.1). That is paid only by machines WITHOUT the ROM: on a ROM
   machine those bytes are themselves in ROM.
   - The re-pointing costs no RAM: the patch lists and the patcher ride in the
     ROM's tail, accepted only under a matching key.
   - `.vgabuf` costs one patched word and one conditional in `mem_floor_ax`
     (§3.4.2).
   - Stage 2 spends about 50–70 transient bytes of its 146 spare.
   - **So the detecting kernel is the plan, and the ROM-required build is not
     needed** (§3.5): any disk boots with or without the ROM, and a
     mismatched ROM is ignored with a sentence.
5. **A ROM is welded to the kernel's LAYOUT, not to the commit count.**
   - `.cold` carries no build number. Two assemblies that differ only in
     `BUILD_NUM` have byte-identical `.cold` (only `.text` and the module
     headers move).
   - But `.cold` names `.text` routines and data by offset, so any change that
     moves one of them re-cuts the ROM. Over the last month an image lasted
     about 2.5 integration commits on `kern_big` (§3.8).
   - **A stable ROM ABI is out of reach** (§3.8). The ROM is a RELEASE
     artefact, and detection falls back to RAM when the two disagree.
6. **System applications in ROM save TIME and the FLOPPY, not RAM — except
   kernel modules** (§4).
   - A package (`.O88`) or driver (`.DRV`) runs `CS = DS = its own segment`
     with its bss inside the image. It can only be COPIED out of ROM.
   - Their ABI is versioned (`PKG_FMT`, `DRV_VER`), so that ROM **survives
     kernel rebuilds**. This is the opposite of form 1's weld, and it makes
     them the right thing to burn.
   - The Control Panel is a kernel MODULE (`CTRL.DRV`). Modules run `.cold`'s
     contract, so they can execute in place and save their whole claim. They
     carry form 1's weld instead.
7. **The 5150 has SIX 8KB sockets, not four, and five of them are spare
   (section 1).**
   - U28 at F4000 is empty. U29–U32 at F6000–FDFFF hold Cassette BASIC. U33
     holds the BIOS.
   - Laid out as ONE option ROM that declares 32KB (section 1.3), the 40KB
     window needs no stub at F600:0000. Its init re-points `int 18h` at an
     "insert a system disk and press any key" stub, and two balance bytes
     keep POST quiet.
   - That holds on 10/27/82 and GLaBIOS. The 1981 BIOSes scan no option ROMs
     and are left out.
8. **`kern_big` built for a ROM is smaller resident than `kern_small` is
   today** (60,928 against 65,536).
   - On a machine with the ROM, the second kernel's reason to exist may go
     away.
   - That is arithmetic and not a measurement. §3.5.1 says what has to be run
     before anyone says it out loud.

---

## 1. The hardware

### 1.1 The IBM PC 5150

| socket | address | stock content |
|---|---|---|
| U28 | F4000–F5FFF | **empty** |
| U29 | F6000–F7FFF | Cassette BASIC, part 1 |
| U30 | F8000–F9FFF | BASIC, part 2 |
| U31 | FA000–FBFFF | BASIC, part 3 |
| U32 | FC000–FDFFF | BASIC, part 4 |
| U33 | FE000–FFFFF | BIOS (10/27/82 on the owner's machine) |

**The recollection in the request was "4 slots, 3 BASIC, one open", and it is
two short.**
- There are four BASIC chips and one empty socket. With U33 left alone, that
  is **40KB of addressable ROM space, contiguous from F4000 to FDFFF**.

**Chip type:**
- The stock parts are 24-pin 2364-type mask ROMs.
- A 2764 or 27C64 EPROM is 28-pin with a different pinout, so it needs a
  "2364 adapter" or a board such as the epictronics IBM-5150-BIOS-Adapter.
- Motorola's MCM68766 is pin-compatible with no adapter.
- No jumper option is known. Board revisions (16–64KB against 64–256KB) are
  not known to differ in the ROM sockets, but this is UNVERIFIED.

**What POST does to these sockets** (from the BIOS listings):

| | 04/24/81, 10/19/81 | 10/27/82 |
|---|---|---|
| option-ROM scan | none | C8000–F5FFF in 2KB steps, **which includes U28** |
| BASIC checksum (each 8KB module must sum to 0) | failure beeps, boot continues | failure prints `F600 ROM` (or F800…), boot continues |
| `int 18h` | F600:0000 | F600:0000 |

### 1.2 The IBM PC/XT 5160

- **1982 board:**
  - U18 is 32KB: BASIC at F8000–FDFFF plus the BIOS.
  - U19 is 8KB, mirrored four times across F0000–F7FFF, so F4000 is a mirror
    and not a free socket.
  - Replacing BASIC means re-burning the chip that carries the BIOS.
- **1986 boards:**
  - POST checksums the **whole 64KB from F0000 to FFFFF and HALTS** on a
    mismatch.
  - Every burn therefore has to re-balance a checksum byte inside the BIOS
    chip.
- **The XT's motherboard is a worse host than the 5150's.**

### 1.3 The 40KB window, laid out as ONE option ROM

`int 18h` is the deciding fact.
- A 5150 with no floppy in the drive retries four times and then executes
  F600:0000.
- If `.cold` were laid flat from F400:0, that address is the middle of
  whatever proc landed at offset 0x2000, run with whatever is in DS.
- That is not a hang to shrug off. It is arbitrary kernel code, and the file
  system is most of `.cold`.

The first draft of this section answered that with a stub at F6000 and
`.cold` cut around it. The BIOS listing has a better answer, which needs
nothing cut and nothing planted inside `.cold`: **make the whole window a
real option ROM.** Five facts out of `PCBIOSV3.ASM` (10/27/82) and GLaBIOS's
source carry it:

1. **The order is right.** On 10/27/82 POST points `int 18h` at F600:0000
   during vector setup (line 610). The option-ROM scan of C8000–F5FFF runs
   long after (line 1098). GLaBIOS is the same: vectors at its step 18, the
   scan at step 28. So an init routine at F400:0003 runs AFTER the vector is
   set, and can point `int 18h` at our own stub. **The stub can live
   anywhere in the tail.** Nothing has to be at F600:0000 at all.
2. **The length byte decides where the BASIC check starts, and 40KB is the
   wrong answer.**
   - After a ROM is found, 10/27/82 advances the scan pointer by the declared
     length. `BASE_ROM_CHK` then checksums 8KB modules from THAT pointer,
     adding 0x200 paragraphs until it reaches FE00.
   - It is a do-while: it checks BEFORE it compares.
   - Declaring 40KB (`0x50`) leaves the pointer at FE00. The loop then checks
     the BIOS, wraps to segment 0000 and checksums **every 8KB of RAM in the
     machine**. Below C800, `ROM_ERR` answers a bad sum with a beep, so that
     is about 120 beeps per power-on.
   - **Declare 32KB (`0x40`).** The pointer lands on FC00. The loop checks the
     one module FC000–FDFFF and stops.
3. **So two bytes balance the whole window.** One goes in the header
   paragraph, so that F4000–FBFFF sums to zero for the option-ROM check. One
   goes in the tail, so that FC000–FDFFF sums to zero for the BASIC check.
   No `ROMPAD` slots, no per-chip sums, no `os88ovlchk.py` exception.
4. **GLaBIOS will not mistake it for BASIC.**
   - It aims `int 18h` at F600 only if four 8KB modules from F6000 EACH
     checksum and have distinct first words. Ours do not, by construction.
   - Its own `int 18h` prints a boot-failure line and waits for a key. Our
     init replaces that anyway, with the same behaviour and our own words.
   - It scans on to FE000 in 2KB steps after a ROM. So FC000, FC800, FD000 and
     FD800 must not start with `55AA`: a 1-in-65,536 chance each, which the
     image builder checks and refuses.
5. **Only the two 1981 BIOSes (04/24/81, 10/19/81) are left out.**
   - They scan no option ROMs, so the init never runs and `int 18h` still
     lands mid-`.cold` on a failed boot.
   - Supporting them would need the first draft's island: `.cold` split into
     an 8KB low part and a high part, with a three-byte `jmp` at F6000.
   - The owner's 5150 is 10/27/82, and the container's MartyPC twins run
     GLaBIOS, so this is left as a stated limitation rather than built.

The image:

```
F4000  55 AA 40  jmp init  <balance>  'OS88'  ...      16 bytes, one paragraph
F4010  .cold, segment F401 (vstart 0)                 kern_big 37,998 / small 24,383
  ...  (kern_small: the XIP modules and the Task Manager image, section 3.7)
FDxxx  tail, ending at FDFFF:
         identity  - signature, format, build, the .cold hash, lengths
         init      - point int 18h at the stub; print "os8088 ROM build N"
                     on the POST screen, which says which image the jumpers
                     picked; retf
         stub      - int 10h teletype "os8088 ROM build N - insert a system
                     disk and press any key", int 16h, int 19h
         <balance> - FC000-FDFFF sums to zero
```

The tail is about 100–160 bytes (ESTIMATE). That leaves **~40,780 bytes for
`.cold`**, and about 2,800 bytes spare on `kern_big` today (§3.6).
`int 19h` re-runs the BIOS's bootstrap. On 10/27/82 that is four tries and
then `int 18h` again, which is our stub: the loop the request described.

### 1.4 The alternative: an ISA ROM board

The ISA option-ROM window (C8000–EFFFF) avoids every problem in §1.3:
- no BASIC checksum;
- no `int 18h`;
- any segment;
- no 2364 pinout.

The Lo-tech 8-bit ROM boards carry 32 or 64KB of flash at a DIP-selected base.
They can be **re-flashed in the machine** with the XT-CF utility, which turns
§3.8's weld from "re-burn four EPROMs" into "run a program". The owner's 5150
already has the ST11M's ROM at C8000, so D0000–DFFFF is the natural window, and
64KB there holds all of `kern_big`'s `.cold` with room for the form 2 set.

### 1.5 Speed

There is no penalty on the target:
- The 5150 adds no wait states to memory. ROM is 250 ns against an 838 ns bus
  cycle, and an 8-bit ISA ROM is zero-wait unless the card pulls I/O CH RDY.
- DRAM refresh holds the BUS, not the RAM, so ROM fetches pay its ~5.56%
  exactly as RAM does.
- The owner's 5150 already measures SixPakPlus ISA RAM and planar RAM within
  0.02% of each other (PERFORMANCE.md Part 8.2).

The cost is on an AT-class machine:
- An unshadowed 8-bit ROM is slower there. That is `FONTSLOW`'s finding about
  a VGA BIOS (SPEC.md 6.0.1).
- A detecting kernel can therefore choose: run from ROM on `CPU_8086`, and
  copy into RAM above that tier when the RAM is there.
- `OSAPI_CPU_INFO` is the fact to test (PERFORMANCE.md rule 7).

### 1.6 The emulators

**MartyPC:**
- Every machine has an optional ROM feature `"custom"` (`machine_config.rs`
  at the pinned `e15cb04`).
- A `[[romset]]` with `provides = ["custom"]` and
  `{ filename = "OS88.ROM", addr = 0xF6000 }` maps it read-only. A filename
  entry skips the md5 check.
- `tools/martypc/build.sh` already copies `os8088_field_roms.toml` into
  `configs/rom_definitions/`, so the row is one more file.
- The 5150 sets here (`ibm5150_82_v4`, `glabios_pc`) are BIOS-only, so
  F4000–FDFFF is empty and free today.

**86Box's `ibmpc82`:**
- It loads `roms/machines/ibmpc82/ibm-basic-1.10.rom` (32KB at F6000) with no
  hash check, so swapping the file works.
- It has **no knob for U28**.
- Its generic ISA ROM board takes a file at C0000–EE000, which covers §1.4.
- **`vm/pc5150/86box.cfg` carries an `[IBM PC (1981)] bios = ibm5150_5700051`
  section belonging to the `ibmpc` machine.** Under `machine = ibmpc82` it is
  ignored, so that VM runs the default 10/27/82 BIOS with BASIC enabled. That
  is unrelated to this plan and was found on the way.

### 1.7 The owner's rig: two One ROMs across U28–U32

The owner tests with a pair of One ROMs (piers.rocks), each wired into
several motherboard sockets. From its documentation:

- **A multi-ROM set serves 2 or 3 images at once.**
  - One socket holds the board.
  - The other sockets' chip-select lines reach its X1 and X2 pins by flying
    lead, and those sockets stay empty.
  - A pair therefore covers five sockets, for example U28+U29+U30 and
    U31+U32.
- **Up to 16 images can be stored**, chosen by the `1/2/4/8` jumpers and read
  at power-on.
  - That is a real answer to "both kernels": both can be flashed and one
    picked by jumper. Section 3.6 has the detail.
  - The init's POST line (section 1.3) says which image came up.
- **Images load over USB.** The ROM set is rebuilt per release and on demand
  during development, which matches the owner's stated workflow.

What the image builder emits for it:
- **Five 8KB files:** `U28.BIN` (F4000) through `U32.BIN` (FC000), one per
  socket. This is the unit One ROM's tooling takes per chip.
- **The 40KB whole:** for MartyPC's `"custom"` ROM row at 0xF4000.

**Access time is answered for one socket.** The owner's 5150 already runs
GLaBIOS off a One ROM in U33:
- That is every instruction fetch of POST, `int 13h` and `int 10h`, served at
  the 5150's bus timing.
- So a One ROM answering a single 2364 socket is fast enough on this machine,
  measured where it matters.

**A multi-ROM set is not answered yet.** Two things are still unknown:
- **The board revision.** The docs say `fire-24-a` cannot build a 2364
  multi-ROM set, because its select GPIOs are not contiguous.
- **The flying-lead path.** It is unknown whether answering a chip select
  that arrives on X1 or X2 is as fast as the native one. The docs raise no
  timing caveat, but say nothing to rule one out either.

**W1's first deliverable is the test for both, with no os8088 code in it:** a
socket-check ROM.
- It is a 40KB option-ROM image laid out exactly as section 1.3 lays out the
  real one.
- Each 8KB socket carries a known pattern and its own sum.
- The init checks every socket and prints `U28 ok … U32 ok` on the POST
  screen.
- A board that drops a byte on a flying-lead socket names that socket.
- Flash it, power on, and read the line.

**This machine's BIOS is GLaBIOS, which is the friendlier of the two.**
- Section 1.3's layout is checked against its source, and the container's
  MartyPC twins run it too. The iron and the emulator therefore take the same
  POST path, which is not true of the 10/27/82 arm.
- If the board in U33 is one of the pair, its X1/X2 pins still serve two more
  sockets. Two boards then cover U28–U33.
- But then re-flashing os8088 re-flashes the board that holds the BIOS. A bad
  flash is recovered over USB, so this is inconvenient rather than dangerous.

---

## 2. What can go into ROM, section by section

| section | can it be ROM? | why |
|---|---|---|
| `.text` | **no** | The near model: CS = DS = `KERNEL_SEG` for kernel code, every task and every `OSAPI_*` cell (`push cs / pop ds`). A ROM `.text` is a different kernel, not a relocation |
| `.bss`, `.lowbss`, `.vgabuf` | no | written |
| `.ovl`, `.ovlw` | pointless | already transient: they cost no RAM by the first desktop |
| **`.cold`** | **yes** | CS of its own, DS = `KERNEL_SEG`, no data, never written (§3.1) |
| on-demand modules (`CTRL.DRV`, `FDLG.DRV`, …) | **yes, mostly** | `.cold`'s contract with a different segment; the writes into their own image are listed in §4.1 |
| packages, drivers | **copy only** | CS = DS = own segment, bss in the image |

---

## 3. Form 1 — the kernel's `.cold` in ROM

### 3.1 Why `.cold` is ROM-clean today

SPEC.md 2.6's cold contract was designed for a different reason (relieving
`KERN_CODE_MAX`). Four rules already enforced by `tools/os88ovlchk.py` are
exactly what a ROM needs:
- no data in `.cold`;
- nothing may take a kernel segment from CS;
- no `.text` table dispatches to a cold body;
- macro arguments are checked as call sites.

A disassembly of both kernels' `.cold` bytes confirms it:
- no write through CS;
- no self-modification;
- no `iret`;
- no memory operand naming a `.cold` label, except `fdlg_htab`'s read on
  `kern_big`.

`kern_small` has no CS reference at all.

**`.cold` needs no change to stop writing to itself, because it never does.**

### 3.2 The segment, measured

`tools/os88romfix.py` copies `kernel/` aside and decouples `COLD_SEG` (the
segment the code executes in) from the ladder rung it occupies in RAM. It then
assembles each kernel at `COLD_SEG` = F601 and F702 and diffs the pair:

| section | `kern_big` | `kern_small` |
|---|---:|---:|
| `.text` | 94 | 60 |
| `.ovl` (blob) | 24 | 7 |
| `.ovlw` | 7 | 3 |
| `.boot2` | 1 | 1 |
| **`.cold` itself** | **10** | **2** |
| on-demand modules | 121 (`.modl` 49, `.modh` 26, `.modc` 21, `.modu` 19, `.modf` 6) | 139 (`.modp` 63, `.modl` 43, `.modd` 15, `.modu` 12, `.modf` 6) |
| **total** | **257** | **212** |

**Every site is a clean word that moved by exactly the delta.**
- No arithmetic on the segment reaches an instruction, and no length changed.
  A patch table is a complete description, and the double assembly is its
  proof of completeness. That is the same trick the old relocating EXE linkers
  used, and the build can run it as a gate.
- Of the `.text` sites, 24 are `OSAPI_FCELL` expansions and 23 are one-line
  thunks.
- Data words holding the segment:
  - `api_coldseg`;
  - `spl_fseg` and `spl_ifseg`;
  - `mod_disarm`'s `COLD_SEG:mod_gone` seed;
  - `compress.inc`'s `cmz_tfp`.

**The ten inside `.cold` on `kern_big`:**
- Eight are cold-to-cold far calls: `ld_pkg_byname`, `fcp_xfer` ×2,
  `fdlg_seed`, `fdlg_reap_x`, `desk_select`, `sc_open` and `hb_ok_x`.
  - `push cs / call near x` replaces each one.
  - That is 4 bytes against 5, so the change SAVES 8 bytes.
  - It needs one line in `os88ovlchk.py`: `push cs` in `.cold` is legal when
    the next instruction is a near call to a far-returning cold body.
- `mod_disarm.slot` stores the segment. It becomes `mov [di+2], cs`, via AX.
- `dsk_xfer.notch` is a sentinel compare, which is §3.3.

`kern_small` has only those last two. After that wave **no `.cold` byte names
its own segment**:
- The ROM image is the same at F601, F401, D000 or E000.
- The kernel patches `.text` to whatever segment it FOUND.
- One image serves the motherboard and an ISA board.
- `os88romfix.py`'s "inside `.cold`: 0" becomes a gate that keeps it that way.

### 3.3 The defect every ROM design must fix first

These sites test `cmp word [spl_fseg], COLD_SEG / jbe` to mean "the blob has
been given back, or was never there":
- `kernel.asm:5375`, `5385`, `5404` (the `SPLCALL`/`OVLCALL`/`OVLCALLC`
  bodies);
- `kernel.asm:6739` (`spl_gate`);
- `dock.inc:214`;
- `sched.inc:2359` (inside `sch_isr`);
- `memory.inc:501`.

The test is correct only because `COLD_SEG` is numerically below every heap
segment. `spl_fseg` only ever holds the seed (`COLD_SEG`) or a live blob
segment, so `je` is the same answer in the same number of bytes, and it does
not care which side of the heap `.cold` lives on. It is worth taking as wave 0
whatever else is decided: it is a latent assumption, and nothing documents it.

### 3.4 Option A — one disk kernel that DETECTS the ROM

**This is the option the owner wants**, on one condition: that it costs few
resident bytes. What it buys is that **nobody has to know which disk they
made**:
- any system disk boots with the ROM, and uses it;
- any system disk boots without it, as today;
- a ROM from a different build is IGNORED with a sentence, never trusted.

What follows is costed against that condition, item by item. The two places
it could have been expensive were the re-pointing and `.vgabuf`, and neither
is.

#### 3.4.1 The resident bill

These are the bytes a machine WITHOUT the ROM pays for the ability. A machine
WITH it pays them in ROM, and gets the whole `.cold` rung back.

| item | where | bytes | how |
|---|---|---:|---|
| re-pointing `.text`, `.ovl`, `.ovlw` and `.boot2` at the ROM (126 sites on `kern_big`, 71 on `kern_small`) | stage 2 and the ROM | **0 resident** | the patch table and the patcher live in the ROM's tail (3.4.3) |
| re-pointing a module loaded later (121 / 139 sites) | `mod_need`, `.cold` | **+15–20** | "the ROM is in, so apply this module's list" (3.4.4) |
| the heap floor, including `.vgabuf` | `mem_floor_ax`, `.cold` | **+9–11** | one more conditional subtract (3.4.2) |
| §3.3's sentinels | everywhere | **0** | `jbe` → `je` |
| §3.2's eight self-calls | `.cold` | **−8** | `push cs / call near` |
| "is the ROM in?" | — | **0** | `cmp word [api_coldseg], COLD_RAM`, a word that exists already |
| hibernate, and the DOS box's live resume | `HIBER.DRV`, staged stub | **0 resident** | the image records the key; a resume against a different ROM, or none, refuses (3.4.5) |
| "ROM build N ignored" | the boot overlay | **0 resident** | the splash says it once |
| **total, per kernel** | | **~+16 to +23 bytes of `.cold`, 0 of `.text`, 0 of `.bss`** | ESTIMATE: no line of it is built |

**The transient bill is in stage 2's `.boot2`:** about 50–70 bytes of its 146
spare. That covers the probe, a far call into the ROM's patcher, and the rule
that stops reading at `.cold` (3.4.3). It costs nothing once the kernel is up.

#### 3.4.2 `.vgabuf`: one rung moves, and nothing else changes

`.cold` sits between the image and the FAT window today. On a ROM machine
that would leave its RAM as a hole in the middle of the kernel, so the ladder
moves it to just under `.vgabuf`:

```
today:      [image][COLD][FAT][LOW][VGABUF] heap
Option A:   [image][FAT][LOW][COLD][VGABUF] heap
```

**A machine without the ROM sees no difference.**
- It has the same rungs and the same total.
- `.vgabuf` is still topmost, so a mono machine still drops the floor under it
  (SPEC.md 39.22).

The heap floor on every machine is then one expression:

```
floor = HEAP_SEG - (mono ? VGABUF_PARA : 0) - (ROM ? COLD_PARA : 0)
```

| machine | floor |
|---|---|
| no ROM, VGA | `HEAP_SEG`, as today |
| no ROM, mono | under `.vgabuf`, as today |
| ROM, mono | `COLD_RAM`: the cold rung AND the idle `.vgabuf` above it, contiguous |
| ROM, VGA | `COLD_RAM + VGABUF_PARA`: the planar decoder's buffers MOVE DOWN into the bottom of the dead cold rung |

The last row is the `.vgabuf` "trouble", and it is one patched word:
- `VGABUF_SEG` reaches running code through ONE instruction, the
  `mov ax, VGABUF_SEG` at `vga12.inc:2568`.
- That is a one-entry list in the patch table, with its own delta of
  `COLD_RAM - VGABUF_SEG`.
- `mem_floor_ax` has two callers, both at boot: `mem_init` and `mem_unblob`.
  Its one new test reads `[api_coldseg]`, which the patch has already set.
- On `kern_small` `VGABUF_PARA` is 0, so the whole row folds away.

#### 3.4.3 Re-pointing: what is baked, what is patched, and the key

**What is baked:**
- `KERNEL.SYS` is assembled exactly as today, against the RAM segment.
- A machine with no ROM therefore runs the kernel `make` assembled, byte for
  byte. Nothing is patched, and no patcher runs on it.

**When the ROM is in, it brings everything needed to adapt the kernel to
itself.** Its tail carries:
- the KEY;
- the patch lists: `.text` and `.ovlw` relative to their own segments, `.ovl`
  and `.boot2` relative to the blob's, and the `.vgabuf` word;
- one per-module list for each on-demand module;
- a small patcher, about 35 bytes, that applies them.

**The ROM can carry kernel-specific data because of the KEY:**
- Stage 2 calls the patcher only when the ROM's key equals the key
  `KERNEL.SYS` was built with, which is 8 bytes in `.boot2`.
- The two are therefore from the same build, the lists are this kernel's
  lists, and no version of the format has to be supported other than the one
  the pair was cut with.

**The key covers `.text` AND `.cold`, not `.cold` alone:**
- It is a hash of both, with the build number's bytes and the fixup sites
  masked out.
- Byte-identical `.cold` does prove `.cold`'s ADDRESSES still agree with
  `.text`. It does not prove the routines behind them kept their CONTRACTS. A
  `.text` routine that started clobbering BX without moving would pass a
  `.cold`-only key and corrupt a machine.
- The report's section 5 history says what the conservative key costs: in
  practice kernel work moves both anyway.
- A mismatch is never a failure. It is a ROM that sits unused until it is
  re-flashed.

**The order in stage 2 (and in the blob's `KZ_HD`, which the hard-disk VBR
already far-calls):**
1. Read and expand everything up to `.cold`. `.cold` becomes the LAST block
   run of `KERNEL.SYS`, with its own destination base: today `os88kz.py` lays
   block *i* at `head + i × BLK`, and this adds one base switch.
2. Probe. Look at F400:0 for `55AA` and the `'OS88'` signature, then compare
   the key. An ISA board's segment is one more two-byte list entry; §3.2's
   W0 makes `.cold` position-independent, so it may sit anywhere.
3. **ROM present:** far-call the ROM's patcher with the delta, and do not read
   or expand `.cold`. That saves about 69 sectors on a floppy `kern_big` boot
   (§3.4.6).
4. **Absent or different:** read and expand `.cold` into its rung, as today.

The floppy loader reads `.cold` only in step 4. The hard-disk VBR keeps
reading the whole file, because it is cheap there and the VBR is full. It only
skips the expansion.

**Integrity:**
- The BIOS has already checksummed the 32KB option-ROM part at POST, on both
  10/27/82 and GLaBIOS.
- The probe sums the last 8KB itself: about 30 ms at 4.77 MHz (ESTIMATE),
  since GLaBIOS does not check that module.

#### 3.4.4 Modules

An on-demand module far-calls `.cold`: 121 sites on `kern_big`, 139 on
`kern_small`.
- A module read off disk on a ROM machine still names the RAM segment, so it
  is patched at load.
- `mod_need` compares `[api_coldseg]` with `COLD_RAM`, and if they differ it
  far-calls the ROM's patcher with that module's list.
- That costs about 15–20 bytes of `.cold`. On a ROM machine they are ROM
  bytes, so the only RAM cost is on a machine without one.
- The alternative is to reach `.cold` through `api_far`'s trampoline from
  modules. That moves the cost into module images, which are not resident,
  but adds cycles to every call. It is not worth it for 20 bytes.

#### 3.4.5 What the ROM changes for hibernate

- A hibernate image is conventional RAM (SPEC.md 87), so it never contains
  the ROM.
- It does contain a `.text` that was patched to it, so the header records the
  key in force.
- A resume against a different ROM, or none, refuses.
- That code is in `HIBER.DRV` and the staged resume stub, and none of it is
  resident.

#### 3.4.6 What the boot saves

These are estimates, from SPEC.md 2.9.13's field figures (~27.2 ms a sector,
39.9 cycles an output byte):

| kernel | sectors not read | read time saved | decode saved |
|---|---:|---:|---:|
| `kern_big` | ~69 | ~1.9 s | ~0.3 s |
| `kern_small` | ~45 | ~1.2 s | ~0.2 s |

Against that, the probe's 30 ms sum. `BOOTPROF=1` on the 5150, with and
without the ROM, is the measurement.

**W2b, ASSESSED AND NOT BUILT (2026-10-05).** The saving above stands as an
estimate, and the two things it needs have different answers:

1. **The identity is solvable without the RAM copy.** Today the ROM's first
   test compares its `.cold` with the one just expanded, which is exactly the
   read W2b wants to skip. The replacement is a `.cold` checksum the KERNEL
   carries in `.text`, made the way `buildnum.inc` is: a generated include in
   the build directory, written between two assemblies. It converges in one
   step because the word is the same SIZE whatever its value, so `.cold`'s
   bytes do not move between the passes - and `os88sym` re-assembles with the
   build directory on its include path already, so it stays exact. The
   Makefile's second pass (SPEC.md 2.9.13) is where it would go.
2. **The loader has no room, and that is what stops it.** Skipping the read
   is stage 2's decision, made before the tail is read: probe F400, compare
   the ROM's checksum with the kernel's (in an EARLIER block, so after the
   first block decodes), and stop reading at a block boundary os88kz.py would
   have to force at `.cold`'s start. Stage 2's `.boot2` is the tightest
   section in the tree - W2's doorbell, about 60 bytes, broke four kern_small
   knob builds there and had to leave (deviation 5) - and this is more code
   than the doorbell, on both loaders (`boot2.asm` and `boothd.asm`'s blob
   entry). So it waits on room in `.boot2`, which is a size pass on the
   loader rather than a ROM change.

#### 3.4.7 What it touches that could bite

- **The ladder reorder moves `FAT_SEG`, `LOW_SEG` and `OVLW_START` for
  everyone.** Rule 5 of the build already re-derives them. Two things on top
  of that:
  - `tools/os88geom.py` and anything else that mirrors the layout has to
    follow.
  - §18.93.1's boot canary (`KSIG_OFF`) rests on where the `.text` sectors
    fall. `.text` does not move in the file, but the band must be re-proved
    with `tests/unit/t_canary.py`, because a canary nobody re-proved is how
    the 1.2MB geometry once made it inert.
- **The `NOKZIP=1` raw loader** reads contiguously. It is a diagnostic knob, so
  it can keep reading `.cold` into its rung and never skip.

### 3.5 Option B — `CROM=1`, a ROM-required kernel

**NOT THE PLAN since §3.4.1 costed Option A at ~20 resident bytes.** The
owner's condition for detection was that it be cheap, and it is. This section
stays as the fallback, and as the record of what was weighed.

- `COLD_SEG equ ROMSEG` (a constant chosen at build), and `COLD_PARA` leaves
  the ladder.
- `KERNEL.SYS` carries no `.cold`.
- Modules are assembled against the ROM segment.
- There are no fixups and no probe beyond a hash check in stage 2. A mismatch
  REFUSES with the ROM's build and the kernel's build on the glass.

Against Option A:
- **It saves the disk as well as the RAM.** About 35KB packed comes off the
  system disk, roughly 35 clusters at 360KB.
- **It saves the same boot time**, because there is nothing to skip.
- **It is simpler.** There is no ladder reorder: `.cold`'s rung is simply
  zero.

What it costs is a fourth kernel artefact:
- It is `KERN_EMU`'s shape (an additive variant with its own build tree and
  `kernsize` baseline).
- It has its own system disks.
- It needs a `buildmatrix` row to keep it assembling.
- And it is useless to anyone without the ROM, which Option A never is.

#### 3.5.1 What `CROM=1` does to the two-kernel split

`kern_big` with `.cold` in ROM has a `KERN_SIZE` of **60,928**, smaller than
`kern_small`'s 65,536 today.

Guard 5's question is whether stage 1 can read the image under the top of a
128KB machine. Arithmetically it would answer yes. `KERN_BUDGET` already says
`kern_big` RESIDES in 128KB.

So **on a machine with this ROM, `kern_big` may run on 128KB with more heap
than `kern_small` has now.** That is unmeasured, and what decides it is the
boot-time claims (`dirw`, the read-ahead) that no assembler sees.
`tests/small128.py`'s shape, run on a `CROM=1` `kern_big` on the floor
machine, is the measurement. Until it is taken, this is a sentence about
arithmetic.

### 3.6 `kern_big` in the 40KB window: it fits today, and the margin is the risk

`.cold` is 37,998 bytes against about **40,780** usable (§1.3), so there are
~2,780 bytes spare (7%). It goes in as one segment, F401, with no split,
no stub inside it and no far calls added.

**The margin is what to watch, because `.cold` has been bigger than the window
recently.** The daily maximum of `kern_big`'s `.cold` on `elendilon`, from the
report's history:

| | bytes |
|---|---:|
| 2026-09-01 | 40,807 |
| 2026-09-03 | 41,168 |
| 2026-09-08 | 38,974 |
| 2026-09-21 | 40,962 |
| 2026-09-28 | 40,847 |
| 2026-09-30 | 40,958 |
| 2026-10-02 | **41,878** |
| 2026-10-03 | 38,446 (kernel size pass) |
| 2026-10-04 | 37,998 (system-side size pass 1) |

It would not have fitted on **8 of the 21 days** with an integration commit
since 1 September. It fits now because of the last two days' size passes. The
file system is most of `.cold`, and file-system features are where kernel
growth has been landing. So the plan needs a guard and a pressure valve:

- **The guard.**
  - The ROM build asserts that `COLD_SIZE` fits the window at assembly time,
    in `kernel.asm`'s guard style.
  - `kernsize` gains a `rom` line, "`.cold` N of 40,780", so the margin is
    printed on every build the way the rungs are.
  - Per CLAUDE.md's rule, a crossing is a conversation, not a build fix.
- **The pressure valve is the split** that the first draft of this section
  recommended for 32KB:
  - The leaves move out to a RAM `.cold`: `apps`, `vga12`, `loader`, `lz`,
    `hiber`, `desksc`, `blank`, `ctrl`, `menu`, `wm`, `instance`, `dock`,
    `extmod`, `clone`, `ui`. That is 5,533 bytes.
  - The report measures **163 crossing sites**. 55 of them go through
    `kernel.asm`'s `cw_*` shims, which can be duplicated. The other ~108 need
    far entries, estimated at 300–500 bytes.
  - It is not needed today. It is what to reach for the day the guard fires.
    Moving one leaf at a time is the same mechanism at a smaller cost: the
    first leaf out buys its own size of headroom.

**Both kernels' `.cold` at once does not fit:** 37,998 + 24,383 = 62,381
against 40,960. **Both kernels on the same rig does**, through One ROM's image
select (§1.7):
- Flash a `kern_big` set and a `kern_small` set, and pick one with the jumpers
  at power-on.
- A ROM-required kernel checks the `.cold` hash before it hands over (§3.5).
  The wrong jumper is therefore a sentence on the glass naming both builds,
  not a crash.
- To check: whether One ROM's image select switches a whole MULTI-ROM set or
  only a single-socket image. The docs describe each feature but not the two
  combined. If it is single-socket only, the second kernel is a re-flash
  rather than a jumper.

### 3.7 `kern_small` in the 40KB window: both forms, with room left

`.cold` takes 24,383 bytes. The rest of the window holds form 2 for this
kernel:

| item | bytes | how it runs |
|---|---:|---|
| `.cold` | 24,383 | in place, segment F401 |
| `FDLG.DRV` | 1,240 | in place (section 4.1) |
| `FILECP.DRV` | 2,269 | in place, once its 36-byte stack moves to `.bss` |
| `CTRL.DRV` | 4,441 | in place |
| Task Manager (small) | 4,980 | COPIED, through `OSAPI_PKG_START`'s image arm, stored unpacked |
| paragraph alignment, the XIP directory, the tail | ~250 | |
| **total** | **~37,560 of 40,944** | **~3.4 KB spare** |

What `kern_small` gets from this:
- **24.5 KB of resident heap back.** On a 128KB machine that is about 62.5 to
  86.5 KB, +38%.
- **The three modules every session reaches cost no heap and no disk read.**
  A Save As onto a data floppy in the only drive works for the first time
  (section 4.4).
- **The Task Manager opens without the system disk.**

Calc (5,312) fits **instead of** the Task Manager, but not beside it.

### 3.8 The weld

- `.cold` names `.text` by offset: **121 far calls into `cw_*` shims** on
  `kern_big` (68 on `kern_small`), and every DS-relative read of kernel data.
- So a `.text` change that moves any of those re-cuts the ROM, even when no
  cold byte of SOURCE changed. The commit count alone does not (§0 finding 5).
- How often it happens in practice is the report's history table. Over the
  82 commits on `elendilon`'s first-parent line since 1 September,
  `kern_big`'s `.cold` changed **32 times** and `kern_small`'s **23**. A burned
  image therefore lasts about **2.5 integration commits on `kern_big` and 3.4
  on `kern_small`**, which is roughly a day of this project's pace. App and
  document merges leave it alone; kernel work re-cuts it. **A ROM tracks a
  RELEASE, not the integration branch.**
- A ROM that survives kernel work would need `.cold` to reach `.text` only
  through a pinned table, and kernel data only at pinned offsets. That is a
  second internal ABI over thousands of references. **It is refused here.**
- What makes the weld livable instead:
  - **Option A falls back to RAM on a mismatch.** A stale ROM costs its saving
    and never a boot.
  - **`make rom` is a release artefact** beside the floppies.
  - **A flash board re-flashes in the machine** (§1.4).

---

## 4. Form 2 — system applications in ROM

### 4.1 Three loaders, three answers

| path | runs as | from ROM | what it saves | version tie |
|---|---|---|---|---|
| package `.O88` (Task Manager) | CS = DS = own segment, bss after the image; on `kern_big` the compactor moves it (`W_SEG`) | **copy only** | load time, the floppy; **no RAM** | `PKG_FMT`, so it survives kernel rebuilds |
| driver `.DRV` (sound, hard disk) | CS = DS = own segment, bss shipped in the image, writes its own variables | **copy only** | load time at boot, the floppy; **no RAM** | `DRV_VER`, so it survives rebuilds |
| kernel module (Control Panel = `CTRL.DRV`) | CS = module, DS = `KERNEL_SEG` (`.cold`'s contract) | **execute in place** | the whole claim (12KB big / 5KB small) plus the load | `MOD_H_BUILD` = the commit count, and `MOD_STAMP` |

**Packages and drivers execute in place only with a new ABI.**
- That would be a split code and data format: a CS word separate from `W_SEG`
  in the window, instance, worker and timer records; every package with its
  data in its own section and no `push cs / pop ds` (13 files have one); and
  `os88pkg.py` and the C `crt0` changed with it.
- Making DS a RAM copy of the whole image saves nothing, so the split format
  is the only version that pays. It is out of scope here and is named so it
  is not re-derived.

**Modules have three blockers:**
- **The stamp.**
  - `mod_check` refuses a module whose `MOD_H_BUILD` is not the running
    commit count.
  - A burned module is refused one commit later, even when its bytes are
    still right.
  - A ROM arm has to compare the cold-layout hash instead. That is the same
    key as form 1, and the right one, because a module is welded exactly as
    `.cold` is.
  - **What shipped for the COPY arm** (SPEC.md 2.10.5): the ROM hands a held
    image out only when its `MOD_H_BUILD` equals the running kernel's
    `mod_hdrt`, so a kernel one commit newer reads the module off the disk
    instead of being refused with no disk tried. The ROM's copies stop
    saving a disk read at the next commit; they never cost a refusal.
- **Writes into the image.**
  - `kern_big`'s `CTRL.DRV` holds `cpc_buf` inside its image and writes
    `cp_fdno`.
  - `HIBER.DRV` writes through CS about 48 times.
  - `kern_small`'s `FILECP.DRV` keeps a 36-byte stack in its claim tail.
  - `FDLG`, `CLONE`, `FORMAT`, `DOCK`, `EXTD` and `kern_small`'s `CTRL` only
    READ through CS.
  - So on `kern_small`, three moves make all three daily modules ROM-able:
    put `FILECP`'s stack back in `.bss` (+36 resident), and that is the whole
    list.
- **`mod_need` claims and frees.**
  - A ROM arm sets the row's segment to the ROM's and runs the check.
  - It skips the zeroing and never frees.
  - Estimate: 30–60 bytes of `.cold`, which is itself in ROM if form 1 is.

### 4.2 Sizes

| item | packed | image (+bss) | RAM claim today |
|---|---:|---:|---:|
| `TASKMGR.O88` | 7,133 | 8,223 + 1,707 | 10,240 |
| `CTRL.DRV` (`kern_big`, ships plain) | 11,649 | 11,649 | 12KB |
| `SOUND.DRV` | 5,474 | 6,307 + 176 | 7KB |
| `HDD.DRV` | 2,994 | 3,568 + 16 | 4KB |
| `HDDTOOL.DRV` (its on-demand half) | 13,995 | 17,398 | 17KB |
| `TASKMGR` small | 4,314 | 4,980 + 1,317 | 6,656 |
| `CALC` small | 4,428 | 5,312 + 413 | 6,144 |
| `NOTEPAD` small | 10,653 | 12,724 + 1,159 | 14,336 |
| `CTRL.DRV` (`kern_small`) | 4,071 | 4,441 | 5KB |
| `FILECP.DRV` (`kern_small`) | 2,061 | 2,269 + 36 | 3KB |
| `FDLG.DRV` (`kern_small`) | — | 1,240 | 2KB |

The `kern_small` module rows come from reports and SPEC.md, and are not
re-measured here.

### 4.3 Getting the bytes out

There are two shapes, and they suit different loaders.

**A ROM VOLUME, for packages and drivers.**
- It is a third transport beside `DVK_BIOS` and `DVK_DRV`/`DVK_FILE`: a
  `dsk_xfer` arm that serves a sector as `rep movsw` out of ROM and refuses
  every write. Estimate: 30–50 bytes.
- It needs a boot-time scan and a volume row.
- Behind it, a read-only FAT12 image in ROM. The whole file layer works
  unchanged: by-name reads, `'CZ'` expansion (so files ride packed), and a
  Disk window that shows `R:`.
- Overhead is ~1.5–2 KB of ROM for the BPB, FAT and root directory, plus
  cluster slack.
- **It must never become the system volume**, because the Control Panel
  writes `SYSTEM.CFG` there. Drivers and modules search it BEFORE
  `[dsk_bootvol]`, through the one seam both use (`drvvol.inc`'s
  `drv_mounted`/`drv_find`).
- `RAMDISK.DRV` is the shape to copy, but not the vehicle. It is a driver, so
  it is loaded off the system disk, which is circular, and `kern_small` has
  no drivers at all.
- The Task Manager can skip even the volume: `OSAPI_PKG_START`'s image arm
  (`ld_pkg_start_x`) already launches a package from a far pointer by COPYING
  it. It wants the image stored unpacked (8,223 bytes), and the menu item's
  "try ROM first" is a few dozen bytes.

**An XIP DIRECTORY, for modules.**
- The ROM header lists module number, segment offset and entry count.
- `mod_need`'s ROM arm points the row at it.
- There is nothing to copy, so there is no volume.

### 4.4 What it buys

**Time, on every launch.**
- A Control Panel open is measured at **1,441–1,570 ms and 4 reads**
  (SPEC.md 2.8.7). A ROM serves it in microseconds.
- Drivers in `SYSTEM.CFG` load at boot, so a ROM `SOUND.DRV` and `HDD.DRV`
  take their reads off every power-on.

**A function, on `kern_small`.**
- `FDLG.DRV` and `FILECP.DRV` are read off the boot volume and **refuse when
  the system disk is out** (SPEC.md 38.0, 22.3.0).
- On a one-drive 128KB machine, a Save As onto a data floppy therefore does
  nothing today.
- In ROM, it works. Of everything in this file, that is the clearest reason
  to do form 2 on `kern_small`.

**RAM, for modules only** (form 2's packages and drivers save none).

### 4.5 Recommended sets

**`kern_small`:** both forms, in the one window. Section 3.7 lists it:
`.cold`, the three daily modules executing in place, and the Task Manager as
the "another app". About 3.4 KB is left.

**`kern_big`:** form 1 alone.
- `.cold` takes the window (section 3.6).
- Form 2's set needs a second home: the Task Manager, `CTRL.DRV`, `SOUND.DRV`
  and `HDD.DRV`, 29,747 bytes unpacked and 27,250 packed.
- The options:
  - an ISA board (section 1.4), whose window C8000–EFFFF does not touch U28–U32;
  - give up form 1 for it, which is the wrong trade, because form 2 saves no
    RAM on this kernel.
- `HDDTOOL.DRV` (17KB) is not needed to USE a disk and is not in the set
  either way.

---

## 5. Recommendation and order of work

The owner's answers settle what section 5 used to ask:
- **The hardware:** two One ROMs across U28–U32, re-flashed over USB.
- **BASIC:** losing it is expected.
- **The cadence:** a ROM set per release, and on demand during development.

The order follows from them.

| wave | what | bytes | why here |
|---|---|---|---|
| **W0** | Section 3.3's nine `jbe` → `je`. Section 3.2's eight `push cs` calls and the two stores, so `.cold` names no segment of its own. `os88romfix.py`'s "inside `.cold` = 0" as a fast row | −8 resident | Correct on today's machines, removes a latent ordering assumption, and every later wave stands on it |
| **W1** | First section 1.7's socket-check ROM, which proves the two boards before any kernel code is involved. Then `make rom`: section 1.3's option-ROM image (header, 32KB length byte, init, stub, two balance bytes, the GLaBIOS `55AA` check). It emits `U28.BIN`–`U32.BIN` and the 40KB whole. Plus a MartyPC `"custom"` ROM row at 0xF4000 | 0 resident | The harness every later wave is tested on. First milestone: the init's POST line, and the stub catching a boot with no disk, on GLaBIOS in the container and on the 5150 |
| **W2** | **BUILT:** Option A on `kern_small` (section 3.4): the ladder reorder, `.cold` as the last block run, the probe and the far call in `.boot2`, the patcher and lists in the ROM tail, `mem_floor_ax`'s ROM arm, the `mod_need` hook, and the key in the hibernate header | **+16–23 `.cold`** without the ROM; **−24,576** with it | The biggest relative gain (+38% heap at 128KB) on the kernel with the most room in the window. The floor machine already has its gate (`tests/small128.py`), and every row in the suite is a with-and-without A/B for free |
| **W3** | **BUILT:** Option A on `kern_big`, plus `kernsize`'s `rom` line (a report: `tools/os88rom.py` is the guard) | **+47** without at build 364, **+67** with W4 at build 387 (SPEC.md 2.10.4); **−37,888** with | The same code with a tighter window |
| **W4** | **BUILT, as copies:** `kern_small`'s modules and the Task Manager image (section 3.7), `mod_need`'s ROM arm, `ui_sys_open`'s | **+34 `.text`** on kern_small, **+35 `.cold`** on both (itself in ROM) | Save As with the system disk out, on a one-drive machine - measured, `tests/romnodisk.py` |
| W5 | `CROM=1` (section 3.5), only if Option A meets something on the iron that a matched pair would not | — | Kept as the fallback, not the plan |
| W6 | `kern_big`'s form 2, if a second ROM window ever appears | ~50–100 `.cold` | |

## 6. Open, unverified, and what would settle each

| question | settles it |
|---|---|
| Do the 16–64KB and 64–256KB 5150 boards wire U28–U32 identically? | a board photo or the schematic for each |
| ~~GLaBIOS's scan range, and what its `int 18h` does with no BASIC~~ | ANSWERED from its source (section 1.3): it scans to FE000, and aims `int 18h` at F600 only for four valid BASIC modules |
| Does the 10/27/82 BIOS behave as section 1.3 reads its listing, with a 32KB header at F4000? Is there one module check at FC000, no wrap, and is our init called? | W1's image on the 5150. MartyPC's `ibm5150_82_v4` set, where the ROM is available, is the cheaper first look |
| One ROM: can the board revision serve a 2364 multi-ROM set (not `fire-24-a`)? Does image select switch a whole multi-ROM set? Does the access time meet the 5150's 250 ns? | the boards in hand and One ROM's docs |
| The boot saving (§3.4's table) | a `BOOTPROF=1` boot on the 5150 with and without the ROM |
| `CROM=1` `kern_big` on 128KB (§3.5.1) | `tests/small128.py`'s shape on that kernel |
| Section 3.6's pressure valve, the split: its real far-call cost | build it when the guard first fires; `kernsize` reads it |
| The 1986 XT BIOS's 64KB checksum with a foreign U19 | the listing; MartyPC has the ROM |
