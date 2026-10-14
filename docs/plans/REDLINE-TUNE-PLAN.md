# REDLINE-TUNE-PLAN — Redline learns the machine's chipset, and tunes its bus

**WAVES 0-3 ARE BUILT (2026-10-13); their FIELD RUNS ARE NOT.** SPEC.md
§103.6 is the contract now and this file the design record behind it; §6
says what each wave shipped and what is still owed. The evidence it starts
from is docs/reports/FE3001-VICTORY-2026-10-13.md. The research behind §5 is
docs/reports/CHIPSET-SURVEY-2026-10-13.md. Branch: `redline-tune`, cut from
`elendilon` 2026-10-13.

## 0. Why this exists — one measurement

The owner's Packard Bell Victory 286 streamed video off a PicoMEM at **380
KB/s at half load**, and every attempt to raise that inside the card ran into
the ISA bus. The PicoMEM is an 8-bit card, so the CPU copies each byte with
one 8-bit bus cycle, and the chipset decides how long that cycle is.

On the Victory that chipset is Western Digital's FE3600. Its FE3001 makes an
8-bit cycle **Ts + Tc + R4 wait states** of 62.5 ns each, and R4 is a register
software can write.

| R4 | ns a card byte | VIDDISK at 50% load |
|---|---|---|
| as the BIOS set it | 915 | 379.8 KB/s |
| 10, the maker's own recommendation | 651 | (not run) |
| 8 (7 is the same: an odd value rounds up) | 523 | **563.1 KB/s** |
| 6 | refused: wrong reads | — |

**Three findings carry this whole plan:**

1. **The BIOS ran the bus 29% slower than the chipset maker recommends.** A
   mode that only programs the data sheet's values is worth having on its
   own, and it is no riskier than the board's designers intended.
2. **The bus has headroom past the data sheet, and the board tells you
   where it ends.** At R4 = 6, a verified sweep found 5,108 wrong reads in
   every run, and every device on the bus ran clean at 8.
3. **It is one register on one board.** Every other chipset is a different
   register, port and encoding. Several of them are dangerous to probe, and
   one (the Victory's own) cannot be identified by probing at all.

## 1. The owner's decisions (2026-10-13, binding)

- **Redline houses it.** Redline grows from a benchmark into a CheckIt-style
  tool: inventory, benchmarks and tuning in one app. SPEC.md §103 is
  extended, not forked.
- **Applying a saved setting loads as little as possible, and it is a
  SECOND PROGRAM** (§3.3). A small applier lives on the SYSTEM disk; the
  whole of Redline does not have to. Decided 2026-10-13, after the
  loader-and-re-home shape was weighed.
- **Autodetect first, but a manual pick is the final word.** A dropdown
  names the chipset when detection gets it wrong or cannot answer.
- **Autostart is not part of this plan.** During development the tuner is
  run by hand after each boot. Autostart gets its own session as a
  first-class feature, and this plan only defines what it will call (§3.4).
- **Machine-specific results stay off the shared branches until the owner
  says.** `picomem-fe3001` (PMDMAFE) is parked, not merged.
- **Test hardware is three machines:** the Packard Bell Victory 286 (FE3600),
  a Packard Bell PB286LP (C&T NEAT, from its board photos), and a Compaq Portable III
  (Compaq's own). Nothing else can be run on iron.

## 2. What changes in Redline's contract

SPEC.md §103.2 says today: *"No writes to CPU configuration ports, cache
controls or MSRs."* That sentence stays true of everything Redline does
**unless the user asks it to tune**. The amendment is written into §103
before any code (CLAUDE.md: update SPEC.md *before* the change):

- **Inventory, Bench and Identify stay read-only.** Identify reads only what
  a chipset row has declared safe to read (§3.2).
- **Tuning writes chipset registers, only after an explicit choice, and only
  to a chipset that is identified** — by detection or by the user's own pick,
  which the UI makes them confirm.
- **Every write is verified before it is kept, or put back** (§3.5).
- **Nothing persists in the chipset.** A reset restores the BIOS's values.
  What persists is a settings file (§3.4), and only Apply reads it.

## 3. Architecture

### 3.1 The Chipset page

A fourth view beside Summary, Detailed and Compare, with this flow:

1. **Identify.** It shows what was found, how, and how sure: *"C&T SCAT
   82C235 rev 4 (ID register)"*, *"WD FE3600 (BIOS image match)"* or
   *"unknown"*.
2. **Pick.** A dropdown lists every chipset row. Choosing one that detection
   did not find asks for confirmation, naming the risk in one sentence. A
   row whose write path has never run on real hardware says so (§4).
3. **Report.** Where the chipset can be read back, it shows the current bus
   timing: bus clock, 8- and 16-bit wait states, command delays. Where it is
   write-only (FE3600), it says so, and offers the measurement Redline
   already knows how to take: an 8-bit read timed in ns a byte.
4. **Data sheet.** It programs the maker's recommended values for the
   machine's CPU clock, then measures before and after.
5. **Tune.** The verified sweep (§3.5), ending at the fastest passing value
   plus a margin of one bus clock by default. The user can take the fastest
   instead.
6. **Save.** The chosen values go to the settings file, and Apply uses them.

Bench gains a before/after pairing, so a user sees in Redline's own units what
tuning bought.

### 3.2 A chipset row

Each supported chipset is a table row in its own include (`apps/redline/cs_*.inc`),
in the style of `detect.inc`:

| field | what |
|---|---|
| name and CPU era | as the dropdown shows it |
| `identify` | a READ-ONLY probe returning CF and a confidence. Allowed to touch only the ports this row declares. NULL for a row that cannot be probed (FE3600: §3.2.1) |
| `readback` | the current timing, or NULL (write-only) |
| knobs | each knob: register, bit field, an encoding table, its unit (wait states, bus-clock divisor, command delay) |
| data-sheet values | per CPU-clock band, as the maker's book gives them |
| sweep order | which knob, which direction, the floor never to cross |
| `apply` | writes the knobs: unlock, write, re-lock, with interrupts off |
| status | `VERIFIED` (run on real hardware), `DATASHEET` (encodings read from the book, never run), `IDENTIFY` (identify and report only). Only `VERIFIED` rows tune without a second confirmation |

**The probe order is the safety policy, and it is fixed.** From the survey,
these are hazards to design around:
- writing an "index" to 22h is not harmless (an Intel 82335 has a real
  register there; coarse decoding aliases it onto the master interrupt
  controller);
- 72h/73h can be the clock chip;
- on VLSI SCAMP a READ of EFh resets the CPU;
- several chipsets have a configuration bit that resets the machine.

So the order is:
1. the BIOS ROM image (reads memory only);
2. probes whose ports belong to nothing else on an AT;
3. 22h/23h-family probes last, with the 82335 ruled out first.

No probe writes a register it cannot restore. A probe that must write an
index restores it.

#### 3.2.1 Identifying a chipset that cannot be probed

The FE3600's registers are write-only, and its ports alias the clock chip on
other boards. It is identified by **the BIOS ROM image itself**: an allowlist
of exact fingerprints (ID string, date, and a checksum over the ROM) from
boards known to carry it, starting with the Victory's. Old AMI BIOS IDs
encode some chipsets (`NET` = C&T NEAT) and can serve as a corroborating
signal. No Faraday/WD code was found, so for the FE3600 the allowlist is the
only route.

### 3.3 Small when applying: a second program, on the system disk

The owner's ask: applying a saved setting should not load the whole app.
Redline today is a 20,192-byte image (14,910 compressed on disk) plus 18,786
of bss. That is not yet a burden, but it is the CheckIt it is about to
become.

**Two shapes work, and the second is decided.**

- **(A) Redline as a loader that re-homes.** This is how a parted package
  already works (SPEC.md §20.12.10):
  - the image the kernel launches is a small loader;
  - when a window is wanted, it reads the real program from a part and
    calls `OSAPI_PKG_REHOME`;
  - the loader's region is freed, and the kernel runs the program's entry
    against the part.

  §20.12.5's rule (entry, windows and callbacks in the primary) is met by
  the re-homed program, not the loader. An apply-only launch would apply
  and exit without ever loading the part.
- **(B) A separate applier, `RLAPPLY.O88` (name provisional). DECIDED.**
  - It has no window. It reads the settings file, applies it and exits.
  - It carries the chipset rows' identify and apply code, shared with
    Redline as SOURCE by `%include`, never copied, the way WEAVE and LOOM
    share (WEAVE-SPEC §1.2).
  - **It ships in `SYSTEM/` on the system disk, and Redline does not.**
    That is the deciding use: the machine re-applies its setting at every
    boot from the disk it booted from, a one-drive machine included,
    while the full Redline stays on the apps disks with the other
    applications.
  - It is `TASKMGR.O88`'s and `THEWIRE.O88`'s pattern (SYSAPPS).
  - **`kern_small`'s disks do not carry it** (owner, 2026-10-13). This is
    an AT problem, as §3.7 explains. The 128KB machine is a 5150-class
    8088, with no programmable bus timing to tune.

(A) stays available for Redline's own growth. If the CheckIt version outgrows
a comfortable image, its loader re-homes into the rest. It is not needed for
applying, and this plan does not build it.

### 3.4 The settings file, and what autostart will call

`SYSTEM/APPDATA/REDLINE.CFG`, written by Save and read by Apply. It carries:
- the chipset row's name and version;
- each knob's value;
- **how it was chosen** (data sheet, or sweep with its margin);
- **the BIOS fingerprint at the time.**

Apply refuses, with a sentence, when the fingerprint no longer matches. A
setting tuned on one board never reaches another board, or the same board
after a BIOS change.

Apply is reached two ways:
- from the Chipset page's button, in Redline;
- by launching `RLAPPLY.O88` from `SYSTEM/` (§3.3).

Autostart, when it is built, will do the second. That is the whole interface
between this plan and that session.

### 3.5 The verified sweep, generalised

This is PMDMAFE's design (`picomem-fe3001`, `tests/pmdma/pmfe.inc`), made
chipset-independent:

- **References are taken at the safe setting with interrupts off.**
- **Verify every 8-bit target the machine has.** For each candidate value,
  compare every 8-bit memory the machine has against the references:
  - option ROMs found by their 55AA headers in C000–EFFF;
  - the VGA's or EGA's BIOS;
  - screen memory (read);
  - a PicoMEM's buffer when one is present;
  - any card I/O port known to count (the PicoMEM's 2A3h ramp).
- **Write, then read back, at the candidate:** a pattern into a writable 8-bit
  memory, read back at the safe setting.
- **Any wrong byte restores the safe value before interrupts return.** A
  card's own BIOS code (int 13h on a PicoMEM) runs out of its ROM at
  whatever speed is set.
- **Report what could not be checked:** writes to video memory, the
  keyboard controller, the clock chip and serial ports. Say so on the page,
  with the owner's checklist from the Victory report as the template.
- **The odd-rounds-up finding generalises.** Steps are reported in bus clocks
  where the chipset quantises, so the sweep does not "find" a step that is
  the same setting.

### 3.6 Beyond the bus: the CPU-speed knob

The Compaq Portable III's documented control is CPU speed: INT 16h AH=F0h
sets it (AL = 0 common, 1 fast, 2 high, 3 toggle, 8 auto) and AH=F1h reads it
(github.com/indrekis/compaq_portable_III_conf). It has no bus-timing knob that
anyone has published. This is a different kind of row:
- a firmware call, not a port;
- readable;
- a dropdown, not a sweep.

It belongs on the same page, because "make this machine go faster, safely" is
the page's subject.

### 3.7 Scope: AT-class machines

The cost being removed is an AT's:
- A 286 or 386 drives a separate, slower ISA bus.
- To stay compatible, the chipset pads every 8-bit cycle out to an 8 MHz
  IBM AT's timing; the Victory's BIOS padded it further still.
- On an 8088 XT the expansion bus IS the CPU's bus at the CPU's clock. An
  IBM 5150 or 5160 has nothing programmable: a memory cycle is the 8088's
  four clocks, plus IBM's fixed wait state on I/O.

**Scope is therefore 286 and 386SX/DX ISA machines, on `kern_big`.**

**Turbo XT clones are the possible later extension.** Some of their
chipsets expose wait states: Faraday's FE2010A (the FE3600's XT-era
sibling), the G2 GC100 and C&T's 82C100. On those the knob mostly undoes
waits the turbo added for compatibility with slow cards. That is a smaller
prize.
- **The owner's Toshiba T1100 Plus** (80C86, 7.16 MHz) is the only XT-class
  machine on hand. The owner calls it a **super-bonus stretch goal, after
  everything else**, and it may not be a target at all.
- It is built from Toshiba's own gate arrays, not the clone chipsets above,
  and no register documentation is known. Its 4.77/7.16 MHz switch would be
  a CPU-speed row like the Compaq's (§3.6), if software can reach it.

## 4. What can be supported, and what can be tested

| chipset | identify | knobs known | test machine | status it can reach |
|---|---|---|---|---|
| **WD FE3600** | BIOS allowlist only | R3, R4 (data sheet in hand) | **Victory** | **VERIFIED** |
| Compaq Portable III | Compaq BIOS signature | CPU speed (INT 16h F0h/F1h) | **Portable III** | **VERIFIED** (CPU speed only) |
| PB286LP | **C&T CS8221 NEAT** - read off the board (below), not Intel's | 62h: 8-bit wait states 2-5, READABLE | **PB286LP** | IDENTIFY row exists; its probe already reads 62h |
| C&T NEAT CS8221 | 22h/23h, revision bits | 61h command delays, 62h wait states and bus clock (encodings need the book) | none | DATASHEET |
| C&T SCAT / SCATsx | 22h/23h idx 40h = 01h/04h/13h | 41h clock control (needs the book) | none | IDENTIFY until the book |
| VLSI SCAMP | ECh/EDh idx 00h = D6h | unknown until the book | none | IDENTIFY, then DATASHEET |
| Headland HT18/HT21, G2 GC10x/GC113 | 1EDh/1EFh revision nibble | unknown — **no book found** | none | IDENTIFY |
| OPTi 82C283 | no ID register | 14h bit 0, AT clock /6 or /4 | none | DATASHEET |
| WD76C10 | possible lock-status probe (emulator behaviour, unverified) | unknown until the book | none | DATASHEET |
| ALi M1409, SiS Rabbit, CS8230, CS8220 | weak or none | — | none | not supported |

**86Box can test identification and the absence of a crash**, because it
emulates the configuration registers of NEAT, SCAT, SCAMP, Headland, the OPTi
parts and others. **It cannot test a speed-up**: it does not model bus timing.
It also has no automation, so a run there is the owner looking. The write
sequences are gated the way PMDMAFE's are: `tests/picomem.py`'s `pmfe` leg
models the ports as the data sheet gives them and fails on any write out of
order, with interrupts on, or while locked. That leg's shape becomes one test
leg per row.

## 5. The data books

Every path is a search hit on a bitsavers mirror, which keeps bitsavers'
paths. None could be opened from this container. Full table with sources:
docs/reports/CHIPSET-SURVEY-2026-10-13.md.

**All 22 are in hand (2026-10-13).** The owner fetched every one, in two
batches: 16, then the 6 outstanding. They are real PDFs with searchable text,
3,102 pages in all.
- The retrodev WD76C10A is a scan; the bitsavers `05-WD76C10A.pdf` is the
  text copy of the same part.
- The two VTI Computer Products books (1989, 1990) cover the VL82C100,
  VL82C101B and VL82C201-205 286 parts, which 86Box does not emulate,
  wait-state material included.
- **None of these are committed.** They are third-party documents (172 MB),
  so a session that needs one asks the owner for it.

**First — chipsets with known timing registers, or common:**
- http://bitsavers.org/components/chipsAndTech/CS8221_NEAT_Data_Book_198710.pdf
- http://bitsavers.org/components/vti/pc/VTI_VL82C310_82C311_82C311L_Data_Manual_199201.pdf
- http://bitsavers.org/components/opti/dataSheets/82C283_386SX_System_Controller_199409.pdf
- http://bitsavers.org/components/opti/dataSheets/82C281_OPTi_Cache_Sx_AT_199108.pdf
- http://bitsavers.org/components/opti/dataSheets/82C291_SXWB_PC_AT_Chipset_199410.pdf
- http://bitsavers.org/components/umc/UM82C231.pdf
- http://bitsavers.org/components/umc/UM82C212.pdf

**The FE3600's family and successor** (the FE3001/FE3001A/FE3600 sheets are
already in hand):
- http://bitsavers.org/components/westernDigital/_dataBooks/1991_SystemLogic_Imaging_Storage/16_WD75C10_76C10.pdf
- http://bitsavers.org/components/westernDigital/_dataBooks/1993_SystemLogic_Imaging_Storage/05-WD76C10A.pdf
- https://www.retrodev.com/files/WD76C10A_system_controller.pdf
- http://bitsavers.org/components/westernDigital/_dataBooks/1991_SystemLogic_Imaging_Storage/00_TOC.pdf

**VLSI's other families:**
- http://bitsavers.org/components/vti/pc/VTI_TOPCAT_VL82C286_82C386_PC_AT_Chipset_Oct90.pdf
- http://bitsavers.org/components/vti/pc/VTI_VL82C316_VL82C323_SCAMP_II_199210.pdf
- http://bitsavers.org/components/vti/pc/VTI_VL82C315A_Data_Manual_199206.pdf
- http://bitsavers.org/components/vti/databook/1989_VTI_Computer_Products.pdf
- http://bitsavers.org/components/vti/databook/1990_VTI_Computer_Products.pdf

**386-era, and catalogues:**
- http://bitsavers.org/components/opti/dataSheets/82C391_82C392_OPTi-386WB_PC_AT_Chipset_199012.pdf
- http://bitsavers.org/components/chipsAndTech/CS8230_CS8231_386AT_Chipsets_1989.pdf
- http://bitsavers.org/components/chipsAndTech/1989_Chips_And_Technologies_Short_Form_Catalog.pdf
- http://bitsavers.org/components/chipsAndTech/1990_Chips_And_Technologies_Short_Form_Catalog.pdf
- https://knm.org.uk/datasheets/82c100.pdf
- https://dosdays.co.uk/media/ali/M6117D_Datasheet.pdf

**Not found online:**
- Headland HT12/HT18 and G2 GC101–113 (common, with a readable ID — the
  largest gap);
- a standalone SCAT 82C235 or SCATsx book;
- ACC 2036/2046, Symphony, Suntac, and the Intel 82335 and 82230.

Directories to browse:
- http://bitsavers.org/components/chipsAndTech/
- http://bitsavers.org/components/ali/
- http://bitsavers.org/components/intel/
- https://dosdays.co.uk/topics/datasheets_index.php

## 6. Waves

Each wave ends with a field run on the Victory where it can, and a report in
docs/reports/.

- **Wave 0 — contract. BUILT** (`90b6a62b`). SPEC.md §103 amended as §2 says,
  and a §103.6 for the Chipset page, rows, settings file and Apply.
- **Wave 1 — identify and report, read-only. BUILT** (`90b6a62b`), with
  VLSI's row widened to SCAMP II and TOPCAT off the books and the WD76C10
  and OPTi rows pick-only (their parts answer nothing until written). The
  gate is `tests/csprobe.py`, Unicorn models rather than 86Box: 86Box can
  host a board and cannot be driven or read from here (docs/TESTING.md).
  The Victory's own fingerprint came back in its first field run's
  CHIPSET1.TXT - **BA37h**, not the RetroWeb dump's 2016h - and is on the
  allowlist. OWED: the Compaq's and the PB286LP's reports.
  - The Chipset page with the dropdown.
  - The BIOS fingerprint routine, which prints the Victory's so the allowlist
    can carry it.
  - Identify rows for SCAT, SCAMP, Headland and NEAT, in the §3.2 order.
  - The FE3600 by allowlist.
  - The Compaq by its BIOS.
  - Gates: a CPU-model test per probe that fails on any port outside the
    row's declared set, and a check on 86Box machines with those chipsets.
  - The owner reads the PB286LP's chip markings - DONE from the repair
    photos (2026-10-13), and the board is NEAT, not Intel:
    - **P82C211-12** bus controller, **P82C212B-12** memory controller,
      **P82C215** buffer and **P82C206** IPC - C&T's CS8221 NEAT, the
      12 MHz grade;
    - an **AMD N80L286-12/S** CPU and a 24.000 MHz oscillator beside the
      211 - CLK2IN, so a 12 MHz 286;
    - an Award 286 BIOS (1984-90) in two 27-series EPROMs;
    - C&T's **F82C455** VGA (with its own `VBIOS` EPROM), a WD37C65 floppy
      controller and an MX16C452 for the serial and parallel ports.
    The survey's "Intel-made" was a search snippet about the laptop and is
    wrong (docs/reports/CHIPSET-SURVEY-2026-10-13.md is a dated report and
    is left as written).
    Its first CHIPSET1.TXT (2026-10-13; BIOS 10/23/89, model FCh,
    fingerprint E228h) found the NEAT by itself and read:
    - **62h = 58h**: 8-bit cycles 4 wait states, 16-bit 1, BCLK = CLK2IN/2.
      Bits 7:6 read 01, where the book calls them reserved and zero - the
      survey's DERIVED fingerprint (`62h & C0h == 0`) would have REJECTED
      this board, which is why the probe never adopted it.
    - **64h = 20h**: an 82C212, revision 1 - the B on the chip.
    - **CPU clock 10.77 MHz timed**, where the 12 MHz part and the
      oscillator say 12: the oscillator beside the NEAT reads HAT7600A
      24.000000 MHz in the photos (the black part beside it is a delay line,
      IN/OUT/G), and the BIOS setup has no speed line, only HIGH. It was
      the TIMING: each pass also counted the PIT reads around it, which the
      next build subtracts (SPEC.md 103.6.3.1).
    Its second report, on the corrected build: **11.94 MHz**, the screen
    2,484 ns = 29.6 clocks, the NEAT still 4 waits. The BIOS setup's
    SHADOW RAM line is ENABLE and cannot be changed, so no ROM read is ever
    a clean 8-bit source on this board; the 8-bit I/O read (SPEC.md
    103.6.3.1) is what this board has instead.
  - **The Victory, on the corrected build**: 15.96 MHz timed; after
    RLAPPLY the PicoMEM and C800 ROM both read 520 ns = 8.2 clocks = R4 8,
    the field `Tuned: ~8 waits`, and after a cold boot `Stock: ~14 waits` -
    the FE3600 sum holding on the iron it was fitted to. The screen read
    849 ns at R4 8 against 923 at the BIOS's 14: that VGA, too, holds the
    bus for most of its cycle, which is why the fastest source and not the
    screen is the chipset's.
  - **The Compaq Portable III's board** (RetroWeb's photo): Compaq's own
    Fujitsu gate arrays (MB67324 104475-003, MB672322 104473-004 and
    MB672316U 104474-001), Intel/AMD 8237A-5 DMA and 8259A interrupt
    controllers, a NEC D765AC-2 floppy controller, an NS16450 UART, a 48 MHz
    oscillator (24 MHz CLK2 for a 12 MHz 286) and BIOS EPROMs 109737-002 and
    109738-002. No published registers, as the survey found; the CPU speed
    (INT 16h F0h/F1h) is its only documented knob.
  - **The four reports with the 8-bit I/O read (2026-10-13):**

    | board | clock timed | memory read | I/O read (3F4h) | setting |
    |---|---|---|---|---|
    | Victory, cold | 15.96 | 912 ns = 14.5 clk | 1,060 ns = 16.9 clk | R4 14 (BIOS) |
    | Victory, RLAPPLY | 15.96 | 520 ns = 8.2 clk | 668 ns = 10.6 clk | R4 8 |
    | PB286LP | 11.94 | screen only, 2,542 ns | 1,152 ns = 13.7 clk | NEAT 4 waits (read) |
    | Compaq Portable III | 11.70 | screen only, 1,188 ns | 814 ns = 9.5 clk | none published |

    - **R4 governs 8-bit I/O**: the I/O read moved 6.3 clocks for 6 waits,
      exactly as the memory read did. The book's "8-bit operation" wording
      meant both; `cs_fewaitsio` now reads R4 from it (SPEC.md 103.6.3.1).
    - **The NEAT's figure fits its book with no fitting**: SYSCLK is
      CLK2IN/4, two CPU clocks, so 2 + 4 waits is 12 clocks, plus 61h's
      default one-BCLK command delay (1 clock) and ~0.7 of synchronisation
      - 13.7, the reading to the tenth. A setting change on it would test
      that prediction rather than fit one.
    - **The Compaq's clock reads 2.5% low** (12 MHz part, 48 MHz
      oscillator), where the Victory and the PB286LP read within 0.5%. Its
      own report at the slow speed is the next reading.
    - **Every VGA here holds the bus**: the Victory's screen moved 925 to
      848 ns for six waits of R4, and both laptops' screens read 13-30
      clocks. A screen read is never a chipset's cycle.
    - **The only 8-bit source was the screen, at 2,479 ns** (26.6 clocks):
      the C&T 82C455 VGA stretches its cycles with IOCHRDY, and the option
      ROM at C000 reads at RAM speed (shadowed). With no clean 8-bit
      device, this board cannot check the measured read against its 4
      waits - a PicoMEM or any plain 8-bit card in it would.
- **Wave 2 — the FE3600, end to end. BUILT** (`e0822f0b`): Data sheet, the
  sweep with its margin question, Save to the BOOT disk's APPDATA and Apply.
  Gated by `tests/csprobe.py`'s write side and `tests/rltune.py` on QEMU.
  OWED: the field run below.
  - Data-sheet mode, the sweep (§3.5), Save, and Apply from the page.
  - PMDMAFE's `pmfe.inc` ported, with its test leg.
  - Field run on the Victory: data sheet, sweep, save, reboot, apply,
    VIDDISK.
- **Wave 3 — the applier. BUILT** (`43c1cc7e`): 1,629 bytes, no window, a
  toast per verdict, on the four kern_big system disks. OWED: the launch
  time on a floppy, and the field run below.
  - `RLAPPLY.O88` built, sharing the chipset rows with Redline as source.
  - It goes into `SYSTEM/` on the system disks.
  - Measured:
    - its image and its launch time on a floppy;
    - a refusal on a fingerprint mismatch.
  - Field run: tune in Redline, reboot, run `RLAPPLY`, check with VIDDISK.
- **Wave 3.5 — the Tune TAB, from the first field run. BUILT.** The
  owner's run found four things, and each is a change: (1) the page was a
  second window and should be a TAB of REDLINE's own - Benchmark and Tune
  now, Info and Test later; (2) every action should be a BUTTON, with only
  the buttons a user needs - Measure went (entering the tab and every write
  measure), Apply went (RLAPPLY is the one way back), and Data sheet became
  Book value; (3) Tune's question vanished unread, because the handler
  repainted the page unclipped straight over its own alert, so every
  handler repaint now arms `OSAPI_WM_CLIP_SET` and Tune asks nothing - it
  SETS the fastest that read right, with Book value as the margin a button
  away; (4) the Victory SHADOWS its VGA BIOS (C000 read 193 ns, C800 913),
  so the sweep's ROM reference is now the slowest ROM measured, and none
  under 300 ns. SPEC.md §103.6 and §103.6.3 carry it.
  A second pass on the owner's word: `Book value` was unclear as a button,
  so it became the second answer of a **Target** drop-down beside Chipset
  (Fastest tested / Mfg spec) and Tune sets whichever is chosen; and a
  framed **state field** after the two says what the bus runs at now -
  Stock, Tuned: n waits, Mfg spec: n waits.
- **Wave 3.6 — the state READ OFF THE BUS. BUILT** (SPEC.md 103.6.3.1). A
  record of what was written goes stale both ways (a no-POST Restart keeps
  the chipset's setting, a hibernate resume loses it), so with nothing set
  this session the field is the timed 8-bit read, in timed CPU clocks,
  through the FE3600's own cycle sum. One constant in it is fitted on the
  Victory. **What the other two 286s can and cannot test**:
  - **Neither is an FE3600 board**, so neither can test the FE3600 sum.
    Only another FE3600 board can, and the allowlist's next entry is how one
    arrives.
  - **The Compaq Portable III tests the CLOCK**, which the sum rests on: it
    has Compaq's own ASICs and no bus knob, but its speed switch changes the
    CPU clock, so one report at each speed shows whether the timed clock
    tracks it. Its read in clocks is also a second board's baseline.
  - **The PB286LP is a C&T NEAT board**, so it cannot test the FE3600
    sum either - but it is the best check of the METHOD on the iron we
    have: NEAT's 8-bit wait states READ BACK (62h bits 3:2, 2-5 waits), so
    one report carries both the truth and the measurement beside it.
  - **The readable chipsets do not need a prediction at all.** NEAT, SCAT,
    SCAMP and Headland keep their wait states in registers that read back
    (CHIPSET-REGISTERS A1.3, B1.3 and on), so a row for one of them reads
    the setting; measuring is the write-only FE3600's answer, not the
    general one.
- **Wave 3.7 — the NEAT row, and ONE SWEEP FOR EVERY ROW. BUILT**
  (SPEC.md 103.6.2, 103.6.3, 103.6.3.2). On the owner's word (open question
  3, answered below): a DATASHEET row SWEEPS, after a warning, because a row
  that may not sweep until verified can only be verified by a second build
  and a second report, once per board.
  - `csne.inc` is the NEAT's knob, 62h bits 3:2, Mfg spec 4 (what the
    PB286LP runs), floor 2. It reads back, so the page reads it live and
    the sweep checks the knob took. RLAPPLY carries it: 1,776 bytes.
  - `csweep.inc` is the sweep, lifted out of `csfe.inc` and driven by a
    row DESCRIPTOR. Its references are read at HOME, not the book's value -
    the first build read them at 10, faster than the Victory's BIOS runs,
    which a board that cannot run 10 would have passed. It gained the
    floppy controller's status, an I/O WRITE read back (DMA page 85h), the
    knob's read-back, a time for every step that passed and a log of every
    step, for the report.
  - The report carries the BIOS's text, the NEAT's registers 60h-6Fh, the
    raw PIT counts and the sweep's log - the owner's "one round trip" rule.
  - **FIELD RUN (2026-10-14), and the NEAT is VERIFIED.** The PB286LP
    swept 4, 3, 2 with every check passing, twice; the second time its
    C000 ROM was no longer shadowed (68h read 0Fh in the first report and
    00h in the next two, 65h 9Fh to 1Fh), so the ROM was read back at each
    step as well as the screen, 3F4h, 85h and 62h itself:

    | NEAT 8-bit waits | ROM C000 | I/O read (3F4h) | book sum, I/O |
    |---|---|---|---|
    | 4 (BIOS) | 877 ns = 10.5 clk | 1,148 ns = 13.7 clk | 13.7 |
    | 3 | 699 ns = 8.3 clk | 969 ns = 11.6 clk | 11.7 |
    | 2 (floor) | 524 ns = 6.3 clk | 795 ns = 9.5 clk | 9.7 |

    - **A wait state is 176 ns** - one SYSCLK, CLK2IN/4 = 167.5 ns, within
      5% - on memory and I/O alike. The I/O column is the book's own sum
      (2 x (2 + waits) clocks, one command delay, ~0.7 of synchronisation),
      written down on 2026-10-13 before any setting had been changed, and it
      holds to 0.2 of a clock at all three. The method - time the clock,
      time the read, convert by the book - is right on a second chipset
      with no constant fitted to it.
    - The floor passed, so the 8-bit knob has nothing left on this board;
      the screen read 2,482-2,545 ns at every setting, its VGA holding the
      bus. What is left on a NEAT is the BUS CLOCK (62h bits 1:0) and the
      16-bit waits (bits 5:4), neither of which this wave writes.
  - **The Victory, on the same build**: home measured as R4 14, then 10
    and 8 passed and 6 failed with 5,108 wrong reads, as before - and the
    I/O write and the floppy status both READ RIGHT at 6, so on that board
    the memory side goes first. The sweep's own times put R4 10 on the
    FE3600 sum too (ROM 647 ns = 10.3 clocks against R4 + 0.4 = 10.4; I/O
    799 ns = 12.8 against R4 + 2.7 = 12.7), a setting the sum had never
    been checked at. Its "memory" did not say WHICH reference failed, so
    the log now carries a flag per reference.
  - **The Compaq Portable III at both speeds** (BIOS 01/29/87, B92Ch):
    - **The timed clock tracks the speed switch**: 11.70 and 7.91 MHz, a
      ratio of 1.479 against 1.5, repeatable to 0.1% (11.70 twice). It
      reads 2.5% low at 12 MHz and 1.1% at 8, where the Victory and the
      PB286LP read within 0.5% - a Compaq property, not the method's.
    - **Its 8-bit I/O read is the same in NANOSECONDS at both speeds** (821
      and 818 ns, 9.6 and 6.4 clocks): the gate arrays time the 8-bit bus
      off a clock of their own, so on this board a figure in CPU clocks is
      the wrong unit, and any future row for it reads the bus in ns.
    - Its RAM read is 482 ns a byte at 12 MHz and 720 at 8 (`Raw PIT
      counts`), about 5.8 CPU clocks either way, where `rep lodsb` is 4 -
      wait states on its own memory.
- **Wave 3.8 — the NEAT's other three knobs. BUILT** (SPEC.md 103.6.2).
  The owner's word after the first NEAT run: *"If there is more to tune for
  the PB286LP, then lets do that. The more we try, the more we learn."*
  Its 8-bit waits passed to their floor, so what that bus still spends is
  the 8-bit memory and I/O command delays (61h, one BCLK each) and the
  16-bit waits (62h bits 5:4, one). Each is a descriptor beside the first,
  `DATASHEET` on its own, and Tune sweeps the four in turn. `cs_ne_put` is
  one field write for all four, so RLAPPLY grew 41 bytes (1,817).
  - OWED: the PB286LP's Tune and report. Its log carries every step of all
    four knobs with the screen, ROM and I/O times at each, so one run says
    what each delay is worth in ns and whether 16-bit waits move the screen.
  - NOT TAKEN, for its own round: the BUS CLOCK (62h bits 1:0, CLK2IN/2 ->
    CLK2IN). It doubles every card's timing at once, so a sweep's checks
    say less about it than about a wait state - a card that holds IOCHRDY
    will read right and still be run out of spec.
- **Wave 4 — the Compaq's CPU-speed row.** Field run on the Portable III.
- **Wave 5 onward — one chipset a wave, as books arrive.**
  - NEAT first: the most common, with known fields.
  - Each lands as DATASHEET (dropdown-confirmed writes), with a port model
    test.
  - It is promoted to VERIFIED only by a run on real hardware — the owner's,
    or a field report from a user.

## 7. Open questions

1. **The applier's name** — `RLAPPLY.O88` is a working name.
2. **The default margin** — DECIDED by wave 3.5 as NONE: Tune sets the
   fastest that read right, and the margin is the Mfg spec Target. On
   the Victory one bus clock past 8 is 10, the book's value itself, so a
   default margin would have made Tune buy nothing there. A chipset with
   finer steps may want the question back; method 2 stays in the file
   format for it.
3. **Is a DATASHEET row ever allowed to sweep** — ANSWERED by the owner
   (2026-10-13): YES, after a warning popup, asked once a row. "Otherwise
   when this goes wide we will have to have multiple back and forths with
   remote users." The worst a sweep does is hang the machine once, and the
   reset that clears it restores the BIOS's timing; what is NOT harmless is
   a setting KEPT, which every later 8-bit write (an 8-bit disk card's
   included) runs at - so the help asks for backups and the sweep writes and
   reads back an I/O port of its own (wave 3.7).
4. **The PB286LP** — ANSWERED: C&T NEAT (wave 1's note).
5. **Field reports** — Redline's Identify output on other people's machines
   is how a row reaches VERIFIED without the owner owning the board. Should
   the saved report carry a section made for sending back?
