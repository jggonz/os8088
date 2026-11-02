# PicoMEM under an emulator: the card's own firmware, run beside the CPU

> **COMPLETED - ALL TEN WAVES ARE BUILT (sections 6.1-6.9, and section 8's
> rows), merged to `elendilon` on 2026-10-29.** This is now the design record;
> docs/TESTING.md's *The PicoMEM* is how to use it.
> What is not: the 2.x `.uf2` for the iron (6.7), and `os8088-pm114`'s
> diagnostic commits, which no leg needed. Branch
> `picomem-emulator`, cut from `elendilon` on 2026-10-29. This began as an
> investigation and a proposal. Every number in it
> is a citation or an estimate, and each estimate says so. The firmware
> facts were read off `Elendilon/ISA-PicoMEM` (`main` at `fdd008e`,
> `pm1x-pr` at `5e507be2`); paths below without a prefix are in that repo's
> `src/`.
>
> **The owner's decisions (2026-10-29), which section 9 records in full:**
> the tool lives in **os8088** (`tools/picomem/`), and can be packaged
> separately at the end. It runs **our** firmware, `pm1x-pr`, for **both**
> boards, as a patch stack on upstream's newest public source. **86Box is
> the primary target.** GUS and MPU-401 are **left out**: they are
> `USE_GUS`/`USE_MPU` in the firmware's own CMake, so leaving them out costs
> nothing.
> Our `OS88_DIAG` commits are used where they help. The shim is GPL-2.
> The tool exists for our own testing; anything wider is for later.

## 0. The recommendation, in five lines

1. **Do not write another model of the card. Run the card's firmware.**
   Compile the PicoMEM firmware's device logic for the host, against a thin
   shim that replaces the RP2040's bus loop, PIO, PSRAM, SD and Wi-Fi. That
   gives a C library, **`libpmhost`**, with one call per ISA bus cycle
   (section 4).
2. **The library is the part that carries across.** Each emulator then
   needs only a thin adapter of a few hundred lines that forwards I/O and
   memory cycles, IRQs, DMA, audio, network and mouse (section 5).
3. **86Box first.** It is C like the firmware. It is the only emulator here
   that already has every service the card needs: ISA memory mapping at any
   address, slirp networking, sound mixing, an 8237, and both field
   machines' CPU classes (the PicoMEM 1.x lives in a 286, the 2.x in a 5150).
   It is also what the owner suggested.
4. **MartyPC second**, through Rust FFI to the same library. It answers the
   questions that need cycle timing, such as SB DMA racing, EMS wait states
   and the poll deadlines in `pm_wait`, and it is the default instrument the
   suite already runs on.
5. **Bochs: not recommended.** It is a 386 and is used here only for
   kern_mega. 86Box's `286-mega` covers the one PicoMEM question kern_mega
   has (section 3.3).

Card 1.x and card 2.x are **two builds of the same library**, selected by
the firmware's own board defines. They are not two models. The 1.x comes
first: its firmware is the one os8088 ships (`firmware/picomem1/`), and its
virtual-DMA path is where most of docs/FIELD-NOTES.md 66 happened.

## 1. Why: what testing the card looks like today

No emulator here has a PicoMEM. The tree says so in four places
(`docs/plans/completed/ksp11/picomem.md`, `tests/picomem.py:6`,
`tests/megapmmouse.py:17`, `tools/os88test.py:334`). What the tree has
instead is **three models written by us**:

| model | what it is | what it can't see |
|---|---|---|
| `tests/picomem.py` (1,840 lines; legs `ne sb vs kern mouse pmdma pmsnd`) | Unicorn x86 running slices of the shipped driver against Python port/interrupt hooks written from `ne2000.c` and a disassembled `pmbios.bin` | The firmware. Every answer is our reading of it |
| `drivers/usbmouse/pmsim.inc` (`-DPMSIM`) + `tests/megapmmouse/pmrom.asm` | A stand-in PM BIOS `int 13h` 6000h/10h/11h and a 512-byte option ROM at D000h, for Bochs | Everything except the mouse handshake |
| `os8088_5150_herc_hdd_sb_ems288_gla` (`emspico` row) | MartyPC's Lo-tech EMS moved to 288h/D000h | The PicoMEM's EMS, its wait states, and its BIOS |

FIELD-NOTES 66 lists **thirteen defects** found on the iron, two of which
were firmware bugs. The 2.x has not been re-checked since the 1.x round.
Each model was written after the fact to pin a defect that was already
known, so it agrees with our understanding of the card by construction. A
model cannot report that the understanding is wrong. That job falls to the
5150 and the Victory 286, one photograph at a time.

**Running the real firmware changes that.** The model would then be the
card's own C, so a disagreement between os8088 and the card would show up
as a failure here rather than on the iron. And because the owner maintains
the firmware fork, **firmware changes get a test bench too**: the A10 fix,
the disk-wait fix and the 512-byte copies were all firmware fixes.

## 2. The card in one page (what an adapter must carry)

These are the facts the design rests on. The agents' reports behind this
section cite file and line for each.

- **I/O:** 10-bit decode, claimed in 8-port blocks through
  `IO_Index_T[port>>3]` (`isa_devices/dev_picomem_io.h:14-41`). The base is
  hard-wired to **2A0h** (`picomem.cpp:232`).

  | port | read | write |
  |---|---|---|
  | +0 | status | command |
  | +1 | data low | data low |
  | +2 | data high | data high |
  | +3 | test counter (+1 per read) | — |
  | +4 | stream | stream |

  A command write puts a token on the core-to-core FIFO, and core 0 runs
  `PM_ExecCMD` (`pm_cmd.cpp:516-1618`). Commands 01h (IRQ ack) and 02h
  (DMA-copy ack) are handled on the bus path itself, not queued.
- **Memory:** a 16 KB BIOS ROM at `ROM_BIOS` (default D0000h; config.txt
  `BIOS xxxx` moves it), followed by **8 KB of shared RAM** (`PM_DP_RAM`).
  Shared RAM layout: config at +82, PC-command registers at +850, IRQ
  multiplexer at +854, `PCCR_Param` at +886, disk buffer at +4096,
  virtual-DMA buffer at +6144.
  - Extra RAM is added per 16 KB block from `MEM_Map[64]` when the PC sends
    command 07h. That means **the memory map changes at run time**.
  - EMS is Lo-tech style at 268h/288h/298h/2A8h with a frame at D000h or
    E000h, up to 4 MB.
  - Wait states: 1.x PSRAM is about 8; 2.x QSPI is about 1.
- **The BIOS to load is the shipped binary** (`src/rom/pmbios.bin` for 1.x,
  `pm2bios.bin` for 2.x). **`pmbios/*.asm` is older than both**: different
  shared-RAM offsets, different command numbers, and the old IRQ dispatch.
  It is not to be rebuilt from.
- **IRQ (1.x):** one jumpered line (3, 5 or 7) driven by a multiplexer
  (`pm_libs/isa_irq.cpp:78-187`). The request list sits at shared RAM +854.
  The ROM's handler acknowledges with `out 2A0h,1` and turns the SB/NE2000
  requests into `int 8+n`.
- **IRQ (2.x, `USE_HWIRQ`):** real lines 3/5/6/7, with the card's own line
  on IRQ 7.
- **DMA (1.x):** the card snoops the PC's writes to 8237 ports 00h-0Fh and
  80h-87h, then asks the CPU through the multiplexer to copy each block into
  shared RAM +6144 (`isa_dma.cpp:106-150`).
- **DMA (2.x, `USE_HWDMA`):** real DREQ/DACK/TC on channel 1 or 3.
- **Reverse channel:** for `int 13h` (command 88h) and EtherDFS (89h) the
  card drives the PC's CPU through a shared-RAM command loop
  (`pm_libs/pm_pccmd.cpp:43-135` against `pmbios/pm_cmd.asm:118-165`).
  `PM_ExecCMD` **busy-waits on shared-RAM bytes the emulated CPU writes**.
  This is the one hard constraint on the design (section 4.3).
- **Devices** (each in `isa_devices/`):
  - AdLib (emu8950)
  - CMS
  - Tandy
  - Mindscape
  - Covox
  - SB 2.0 DSP
  - NE2000 (over CYW43 Wi-Fi)
  - USB joystick and mouse
  - RTC
  - GUS and MPU-401 (2.x only)

  Port maps and enable commands are in section 3 of the firmware report.
  The ones os8088 touches are AdLib (73h), SB (77h/78h), NE2000, EMS, the
  mouse (`int 13h` 6010h/6011h → `int 33h` AX=0060h), and `int 13h`
  floppy/HDD from images, including the FDDn_Attribute B: test.

## 3. The choices

### 3.1 How to model the card

| option | what | verdict |
|---|---|---|
| **A. Native re-implementation** in each emulator (Rust in MartyPC, C in 86Box), loading the real `pmbios.bin` | Write the port block, command set, IRQ multiplexer, virtual DMA, SB, NE2000 and int13 again, by hand | **Refused as the main line.** It is `tests/picomem.py` again in a third language, twice. It tests our understanding rather than the card, it doubles the work for a second emulator, and it drifts every time the owner changes the firmware. |
| **B. Firmware in the loop: `libpmhost`** | The firmware's own C/C++ compiled for the host. Its bus loop is replaced by an API, and its platform calls by a shim | **Chosen.** One body of code, which is the card's, under every emulator. Firmware fixes are testable before flashing. v1 and v2 come from the firmware's own `#if`s. |
| **C. Emulate the RP2040** (Renode, rp2040js, Wokwi) and run the real `.uf2` | Co-simulate a 270 MHz dual-core RP2040 and its PIO programs against ISA bus cycles | **Refused.** The PIO bus capture is cycle-exact against 4.77 MHz bus timing, so co-simulating it means simulating hundreds of ARM cycles per ISA cycle. None of the RP2040 emulators found models PIO at the level these programs need (`rp2040-pio-emulator` runs one state machine and no pin sets). The seam in B sits exactly above the part C would spend its effort on. |

What B gives up, stated up front: everything **below the seam**. That
covers bus-level decode (FIELD-NOTES 66's AEN/A10 defect), IOCHRDY timing,
the 1.x's SD/PSRAM SPI contention, and the PIO programs themselves. Those
stay the iron's job. Section 7 is the list of what B can and cannot catch.

### 3.2 Which emulator

| | 86Box | MartyPC | Bochs |
|---|---|---|---|
| language | C, same as the firmware | Rust (FFI through `cc` in `build.rs`) | C++ plugin |
| field machines | 286 (Victory, 1.x) **and** 5150 (`vm/pc5150`, 2.x) | 8088 only; covers the 5150 | 386 only |
| ISA memory window at D0000h, run-time remaps | `mem_mapping_add` at 4 KB granularity | `register_map`, 8 KB granularity, needs a new `MmioDeviceType` | `DEV_register_memory_handlers` |
| snoop the 8237's ports (1.x virtual DMA) | **Yes.** 86Box's I/O handlers are a per-port list, so a second handler on 00h-0Fh/80h-87h sees the writes the real 8237 also gets. Verify this in wave 1 | **No.** One device per port; `bus/io.rs` needs a snoop hook | Possible |
| real DMA (2.x) | `dma_channel_read/write` | `DMAController` (patch 01's SB is the template) | Yes |
| network for the NE2000 | slirp, already used by `vm/xt-wire` and `pc5150` | **None.** The NE2000 leg would need a backend | slirp |
| sound out | `sound_add_handler` | `SoundSourceDescriptor` (patch 10's Covox is the template) | Yes |
| timing | not cycle-accurate | **cycle-accurate**, the suite's default instrument | no |
| scripting in this tree | `tools/os88box.py` (automation socket, `faults`, screenshots, `box-*` rows) | `tools/os88marty.py`, `os88ui.py`, the soak | `tools/os88bochs.py` |
| licence | GPL-2, same as the firmware | MIT; a local build linked to a GPL library is fine and is never redistributed (section 9, Q4) | LGPL |

**86Box first** because it is the only column with every row filled. It
runs both field machines' CPU classes, so the 1.x can be tested in the
286 it really lives in. Its network backend makes the card's NE2000
testable end to end (DHCP through slirp, as `xt-wire` does). And the
adapter is C calling C.

**MartyPC second**, for what only it can say. Is `pm_runaway`'s
32-byte/100-reads threshold right at 4.77 MHz? Does `pm_wait`'s 9-tick
deadline hold? What does EMS cost at 8 wait states (`videmsslow`)? Those
are timing questions, and 86Box is not cycle-accurate. The adapter is the
same library behind `extern "C"`. MartyPC lacks the 8237 snoop, so that is
one hook in `bus/io.rs`, and it lacks a NIC, so the card's NE2000 is left
out there (it is 86Box's job).

### 3.3 Why not Bochs as well

Bochs is here only because it faults like a real 286 does on kern_mega
(docs/BOCHS.md), and its CPU is a 386. The one PicoMEM item kern_mega has
open is servicing the multiplexer's reflected IRQs in protected mode
(KERN-MEGA-PLAN: 61 reflections a second at 3.3 ms each). That item wants a
286 with a real reset path, and 86Box's `286-mega` profile with the card
added is that machine. Once `libpmhost` exists, a Bochs plugin is an
estimated ~400 lines. That is cheap enough to build the day a question
needs it, and no question needs it today.

## 4. `libpmhost`: the design

### 4.1 Where it lives

**In os8088, under `tools/picomem/` (the owner's decision).** It is an
os8088 test instrument, used by our tools and soaks. It can be packaged up
for outside use at the end, but it is not kept in the firmware fork. The
layout follows `tools/86box/` and `tools/martypc/`, the two instruments
this tree already builds from a pin plus patches:

| path | what |
|---|---|
| `tools/picomem/UPSTREAM` | the upstream firmware repo and the commit it is pinned at |
| `tools/picomem/patches/fw/*.patch` | **our firmware changes**, the `pm1x-pr` commit as a patch on that pin. These are the same changes the iron runs |
| `tools/picomem/patches/host/*.patch` | the few edits the host build alone needs (`PM_HOST_YIELD()` where a spin polls shared RAM without reading the clock, section 4.3). Empty on the iron, so they could also go into `fw/` once proven |
| `tools/picomem/shim/` | os8088's own source: stub `pico/*.h` headers, the clock, the FIFO, the event queue, FatFS `diskio` over a file, the `pm_*` API of section 4.2, and the CMake that picks the firmware's files |
| `tools/picomem/build.sh` | fetch at the pin, `git apply` `fw/` then `host/` in order, build `build/picomem/libpmhost-{v1,v2}.{a,so}` |

**Nothing from the firmware is committed to os8088**, only patches and the
pin. That is the same footing as 86Box and MartyPC. The shim is written
against the firmware's interfaces, so a moved or renamed file fails the
build loudly rather than silently.

**Keeping up with upstream.** When FreddyVRetro publishes newer source:

1. Move `UPSTREAM`'s pin.
2. Re-cut `fw/` against the new pin. The `pm1x-pr` branch in the fork is
   where that rebase is done, and the patch is regenerated from it.
3. Rebuild.

`tools/martypc/README.md:24-44` is the rule for cutting a patch: against
the pin **plus every earlier patch**, never against the pin alone.

**The diagnostic knob comes along for free.** `os8088-pm114` is the same
firmware plus `OS88_DIAG` (DK01, DMALOG2 and the AH0x records PMDMA reads),
and it builds identically with the knob off. Carrying its commits as one
more `fw/` patch would give the emulator an `OS88_DIAG=1` build, so PMDMA's
K, A and B keys could be checked against something before the iron
(section 9, decision 4).

### 4.2 The seam and the C API

The firmware already has the seam. Every bus cycle in
`pm_isa_rp2040.h:164-390` (1.x) and `pm_isa_rp2350_m2_v15.h:176-360` (2.x)
ends in `dev_X_ior(port, &data)`, `dev_X_iow(port, data)` or a
`RAM_Offset_Table[MEM_Index_T[a>>13]][a]` access. `isa_devices.cpp:60-84`
even has ready-made `dev_ior[]`/`dev_iow[]` tables indexed by device type,
which are not compiled today. The host build compiles those tables and
puts this API on top. **This is a sketch to argue with, not a contract:**

```c
pm_t *pm_create(const pm_cfg_t *cfg, const pm_host_t *host);
  /* cfg:  board (PM1x|PM2x), BIOS image path, SD image path, jumpers
           (IRQ line, DMA ch), Wi-Fi on/off.
     host: callbacks - irq(line, level), dma_req(ch, on) [2.x],
           dma_xfer(ch, byte*) [2.x], map_changed(), net_tx(buf, len),
           audio_rate, log(level, text). */
bool pm_io_read (pm_t *, uint16_t port, uint8_t *v);  /* true = card drives the bus */
bool pm_io_write(pm_t *, uint16_t port, uint8_t  v);  /* includes 8237 snoop ports  */
bool pm_mem_read (pm_t *, uint32_t a, uint8_t *v);
bool pm_mem_write(pm_t *, uint32_t a, uint8_t  v);
int  pm_mem_waits(pm_t *, uint32_t a);  /* IOCHRDY wait states for this block */
int  pm_mem_map  (pm_t *, pm_range_t *out, int max);  /* windows the card decodes now */
void pm_advance  (pm_t *, uint64_t ns); /* run card time: timers, core 0, audio */
int  pm_audio    (pm_t *, int16_t *lr, int frames);
void pm_net_rx   (pm_t *, const uint8_t *, int);
void pm_mouse    (pm_t *, int dx, int dy, int buttons);
void pm_destroy  (pm_t *);
```

**What is compiled** (the firmware report's "mostly pure C" list):

- `dev_picomem_io`, `dev_memory`, `dev_adlib`/`cms`/`tandy`/`mindscape`/
  `covox`/`sbdsp`/`dma`
- `pm_cmd.cpp`, needing `#if` guards around cyw43, tusb, I2C and OLED
- `pm_libs/pm_pccmd.cpp`, `isa_irq.cpp`
- `audio_devices` (`emu8950.c` with `EMU8950_ASM=0`, `emu2149`, `square`,
  `sbdsp`, `covox_dac`, `audio_fifo`)
- `ne2000.c` with the CYW43 send/receive split out
- `dosbox/bios_disk.cpp`, `pm_libs/pm_disk.cpp`, FatFS
- 2.x adds `gus-x.cpp` and `dev_mpu` later

**What is replaced, not ported:**

- `picomem.cpp`'s clocks, GPIO and `main`
- the core-1 assembly loops and `*.pio`
- PSRAM drivers (host arrays instead)
- the SD SPI driver (FatFS `diskio` over a host file)
- USB host and tinyusb (replaced by `pm_mouse` into the firmware's mouse
  report path)
- CYW43 (replaced by `net_tx`/`pm_net_rx`)
- `pico_pic.c`'s alarm pool (replaced by an event queue in card time)
- the I2S mixer output (replaced by `pm_audio`)

### 4.3 The hard part: core 0, and determinism

On the iron, core 0 runs `PM_ExecCMD` concurrently with the PC. During
commands 88h and 89h it spins on shared-RAM bytes **that only the emulated
CPU will write**. Calling `PM_ExecCMD` straight from `pm_io_write` would
deadlock the emulator against itself.

The design: **core 0 runs on its own host thread, but holds a baton.** The
emulator owns the baton except inside `pm_advance`. `pm_advance` hands it
to core 0 and takes it back at the first yield point. Yield points are:

- **`time_us_64()` and `sleep_us()`.** In the host build these return card
  time, which `pm_advance` moves, and yield. Every spin that reads the
  clock therefore advances only as the emulator does.
- **`PM_HOST_YIELD()`**, a macro the fork adds at the few spins that poll
  shared RAM without reading the clock (`PC_Wait_CMD_End` and its
  siblings). It is empty on the iron, so the shipped image does not change.
  Auditing those loops is wave 0's first job.
- **The FIFO pop** in core 0's main loop, when nothing is queued.

The baton makes runs **deterministic**: the same emulator inputs give the
same card behaviour on every run, whatever the host is doing. That is a
requirement for the suite (docs/WRITING-TESTS.md), not a nicety. A free-
running second thread would make every row's verdict depend on host load.
That is the kind of row SOAK-PARALLEL §1 shows failing in a way that looks
like the feature under test.

Card speed is a separate question. With the baton, the card is infinitely
fast between yields. On the iron a command takes microseconds and a
512-byte SD read takes milliseconds. `pm_advance` therefore charges
modelled costs per operation: SD sector, PSRAM access, command dispatch.
These come from a table seeded from the field reports and **labelled as
estimates**. MartyPC's timing rows read against that table, and a
disagreement with the iron is a table fix.

### 4.4 v1 and v2

These are two builds of `libpmhost` from **one source**: upstream at the
pin plus `pm1x-pr`. The owner's choice is **our own code on both boards**.

- **`PM1x`:** `PICOMEM_BOARD "PICOMEM"`, i.e. `firmware/picomem1/v1board.patch`'s
  defines, with no `USE_HWIRQ`/`USE_HWDMA`. This is exactly what os8088
  ships as `PM_W_10_16_26_OS8088.uf2`.
- **`PM2x`:** `PICOMEM_BOARD "PICOMEM_20"`, i.e. the branch's own default
  `CMakeLists.txt`, with `USE_HWIRQ`, `USE_HWDMA`, 256 KB fast RAM and QSPI
  PSRAM. It takes **`USE_GUS=0 USE_MPU=0`** (the owner's decision). The
  board block turns both on (`CMakeLists.txt:257-258`), and these flags are
  the firmware's own compile-time gates, so leaving them out is free.
  `USE_CDROM` is already 0 on every board.

**What the v2 build is and is not.** It is `pm1x-pr` running on a 2.x. That
combination has **never been tried on the iron**: the 5150's card runs
FreddyVRetro's closed `PM2-10-4-26`. So the emulator's v2 answers *"what
would our firmware do on a 2.x"*. That is the question the owner wants
answered, and it is not the same as *"what does the 5150 do today"*. The
two differ in exactly the places listed for `PM2-10-4-26` in section 10.

There is a useful side effect: wave 6 makes the first `PM2x` build of our
source. A real `.uf2` of the same source is one `cmake` away (section 10),
and it is the image to try on the 5150 once the emulator is happy with it.

The adapter asks which build it was handed and wires:

The adapter asks which build it was handed and wires:

- **1.x:** one IRQ line plus the 8237 snoop.
- **2.x:** four lines, a real DMA channel, and the RTC.

### 4.5 The SD card

The firmware reads `config.txt`, `wifi.txt`, `/FLOPPY` and `/HDD` off FAT
through FatFS. The host build keeps FatFS and points its `diskio` at a
**raw SD image file**: a FAT32 image built by a small host tool that copies
in floppy images, a VHD, and a `config.txt` (`BIOS D000`, `IRQ 5`, ...).
Configuration is then exactly the card's own, and a profile's whole PicoMEM
setup is one file beside its `86box.cfg`. A host directory instead of an
image is a convenience to consider later, not in wave 1.

## 5. The adapters

### 5.1 86Box

The adapter is a new device, `src/device/isa_picomem.c`, carried as
`tools/86box/patches/07-picomem.patch`. Its `device_t` config offers:

- the board (1.x or 2.x)
- the BIOS and SD image paths
- the IRQ jumper and the DMA channel

It wires the following:

- **I/O:**
  - `io_sethandler` on 2A0h-2A7h, plus each block the firmware claims,
    re-read after every command.
  - A snoop handler on 00h-0Fh and 80h-87h on 1.x builds, which forwards
    writes and never drives reads.
- **Memory:** `mem_mapping_add` per range `pm_mem_map` reports, re-run
  on `map_changed`. The firmware can fill holes in conventional RAM, so this
  includes blocks below A0000h on a machine configured with less.
- **IRQ:** `picint`/`picintc`.
- **DMA (2.x):** `dma_channel_read`/`write`.
- **Sound:** `sound_add_handler`, pulling `pm_audio`.
- **Network:** `network_attach`, wired to `net_tx`/`pm_net_rx`.
- **Mouse:** when the card's mouse is on, the profile's mouse goes to
  `pm_mouse`.
- **Time:** a `timer_add` that calls `pm_advance` with the emulated time
  since the last call.

The CMake change links `build/picomem/libpmhost-{v1,v2}.a`.
`tools/86box/build.sh` builds the library first.

New profiles, each a copy of an existing one with the card added:

- `vm/286-picomem1`: `286-victory`, minus the SB and the NE1000, which the
  card provides.
- `vm/xt-picomem1`: an XT with the 1.x, the machine the card was designed
  for.
- `vm/pc5150-picomem2`: `pc5150` with the card, 2.x.

Each boots `make picomemtest`'s `pmsys720.img`/`pmsys360.img`, which
already exist (Makefile 5536-5584).

### 5.2 MartyPC

The adapter is `crates/marty_core/src/devices/picomem.rs` with a
`build.rs` that links `libpmhost`, carried as
`tools/martypc/patches/17-picomem.patch` and cut against the pin plus
patches 01-16 (`tools/martypc/README.md:24-44`). It implements:

- `IoDevice` for 2A0h and the claimed blocks
- `MemoryMappedDevice` for the windows, with a new `MmioDeviceType`
  variant; 8 KB granularity fits the card's 16 KB ROM + 8 KB RAM at D0000h
- `pic.request_interrupt`
- `pm_mem_waits` charged as bus wait states, which is the point of
  bringing MartyPC in

New machinery it needs:

- **An I/O snoop hook** in `bus/io.rs` for the 8237's ports.
- **A `[machine.picomem]` toml key**, alongside the existing `[machine.ems]`.

New machines:

- `os8088_5150_herc_pm1_gla`
- `os8088_5150_herc_pm2_gla` (a 5150 with the 2.x is the real 5150 #2)

**The NE2000 is left out on MartyPC.** There is no network backend there,
and docs/TESTING.md's list already sends the NIC to QEMU and 86Box.

## 6. Waves

Each wave ends with something runnable and a named check. The tiers follow
the Testing section of CLAUDE.md: host checks per wave, and one soak union
plus `test-full` after the last.

| wave | delivers | done when |
|---|---|---|
| **0. Spike** (estimate 2-3 days; **BUILT**, section 6.1) | `host/` CMake target building `dev_picomem_io` + `pm_cmd` + `dev_memory` + `isa_irq` + `pm_pccmd` with the shim; the baton; the yield audit; a `ctypes` driver | A Python script against `libpmhost-v1.so` reads the 2A3h ramp, runs commands 00h/73h/77h/78h with the real firmware's answers, and serves the BIOS bytes at D0000h. **Its first consumer is `tests/picomem.py` itself**: its `sb`/`vs` legs swap their Python port model for the library, and the legs that pass are now agreeing with the card. |
| **1. 86Box: the card boots** (**BUILT**, 6.2) | `07-picomem.patch`, `vm/xt-picomem1`, the SD-image tool | The PicoMEM BIOS banner and setup menu appear (screenshot via `os88box.py`); `int 13h` 6000h answers DX=AA55h, BX=D000h, CH=jumper; os8088 reaches a desktop with `pm_init`'s 100-read ramp passed. |
| **2. Sound, 1.x** (**BUILT**, 6.3) | AdLib + SB 2.0 with virtual DMA (snoop, multiplexer, CPU copy), audio out | SOUND.DRV's Drivers row reads the SB and the AdLib on the card. A WAV plays and the card's output is captured (an `OS88_PMWAV` env var, in `04-sb-capture`'s shape). `pm_tick`'s AdLib keep-alive matters: removing it goes silent after 4 s, the firmware's own idle cut-off. |
| **3. Disks** (**BUILT**, 6.4) | `int 13h` floppy and HDD from SD-image files | B: is kept through `dsk_fdd_pmemu` (FDDn_Attribute bit 7); HDD.DRV's BIOS rung reads a VHD through the card; the warm restart (1234h, FFFF:0) re-enters the PicoMEM BIOS. |
| **4. NE2000** (**BUILT**, 6.5) | the card's NE2000 over 86Box slirp | ETHER.DRV finds it at the configured base with the un-doubled PROM; DHCP and a fetch as in `tests/ethernet.py`. |
| **5. Mouse and EMS** (**BUILT**, 6.6) | `pm_mouse_move`; the card's EMS at 288h/E000h | USBMOUSE.DRV's PicoMEM backend moves the pointer (`int 33h` AX=0060h through the multiplexer); EMS.DRV finds 288h/D000h on the card instead of on the Lo-tech model. |
| **6. The 2.x** (**BUILT**, 6.7; the machine is `vm/xt-picomem2`) | `libpmhost-v2` (`pm1x-pr` built `PICOMEM_20`, no GUS/MPU), real IRQ lines, real DMA, `vm/pc5150-picomem2` | SOUND.DRV configures the SB through command 77h on our firmware's 2.x build. If the public source still ignores the IRQ/DMA it is given (`PM2-10-4-26`'s note, section 9), this is where we find out, and the fix goes into `fw/`. This is the **first run of our code on a 2.x anywhere**, and it yields a `.uf2` to try on the 5150. |
| **7. kern_mega** (**BUILT**, 6.8; the machine is `vm/at286-picomem1`) | `vm/286-mega` with the card | The reflected multiplexer IRQs are visible and counted under `faults`/E9; this gives the open KERN-MEGA item a machine. |
| **8. MartyPC** (**BUILT**, 6.9) | `17-picomem.patch`, the snoop hook, two machines | `pm_runaway`'s detection is measured on a 4.77 MHz 8088 against the card's own DMA pacing; `videmsslow`'s premise (8 wait states) is read off a real card model. |
| **9. Suite and docs** (**BUILT**: section 8, docs/TESTING.md *The PicoMEM*) | rows, profiles, TESTING.md, FIELD-MACHINES | Section 8's rows registered; docs/TESTING.md's matrix gains a PicoMEM row; CLAUDE.md's emulator lists name the profiles. |

**Estimates, all unmeasured:**

| work | estimate |
|---|---|
| waves 0-3 | 2-3 weeks |
| waves 4-7 | 1-2 weeks |
| wave 8 | about 1 week |

Wave 0 is where the estimate is weakest. The yield audit and the `pm_cmd.cpp`
guards could take longer than they look. If they do, the fallback is option
A for the base-port block only, with the device files still compiled from
the firmware.

### 6.1 Wave 0, as built (2026-10-29)

**What exists.**
- `tools/picomem/` holds four things:
  - `UPSTREAM` pins FreddyVRetro/ISA-PicoMEM `be7b2e6`.
  - `patches/fw/01-pm1x-pr.patch` is `pm1x-pr`, byte for byte the firmware
    in `firmware/picomem1/`.
  - `patches/host/01-pmhost.patch` holds four `PMHOST`-only hunks.
  - `shim/` holds the SDK stand-in headers, the card thread, the clock,
    the FIFO, the alarm pool, the SD image and the API.
- `build.sh` builds `build/picomem/v1/libpmhost-v1.so`, fetch included, in
  **6.5 seconds**.
- `tools/picomem/pmhost.py` is the ctypes driver and the self-check.
- `tests/picomem.py` gained the legs `sbfw` and `vsfw`. Suite row `pmlib`
  (soak) runs them, under a new `pmhost` capability that the runner and
  `os88soak.py check` both report.

**How much of the firmware it is.**
- Compiled as-is: `picomem.cpp`'s `main` and init, the whole of
  `pm_cmd.cpp`, every ISA device the 1.x builds, the IRQ multiplexer and
  virtual DMA, the OPL (emu8950's own host path, `PICO_ON_DEVICE` unset),
  the SB DSP, `bios_disk.cpp`, `pm_disk.cpp`, FatFS and the Qwiic scan.
  That is 47 firmware files.
- Replaced: the PIO bus loop (`shim/include/pmhost_isa.h`, the same
  tables and device calls, transcribed), the SDK, the SD SPI driver and
  the I2S driver.
- The link needed **no stub of any firmware function**. Every undefined
  symbol was an SDK one or the SD/I2S drivers.

**The seam held, with two corrections that will matter to every adapter:**

1. **The memory table is bus-shaped.** `MEM_Index_T` is indexed with
   A16-A19 inverted (`addr ^ 0xF0000` on a 1.x, `^ 0x00F00` on `MUX_V2`),
   because that is how the pins arrive. `GetMEMIndex`/`GetPortType` do it,
   and the shim calls them rather than the raw tables.
2. **The memory pointers assume a 32-bit address space.**
   `RAM_Offset_Table` holds `&PMBIOS[-ROM_BIOS]` and the like, several of
   them negated as `uint32`. That wraps on a Pico and is 2^32 out on a
   64-bit host. `pmh_cell()` undoes the wrap and checks that the result
   lands in one of the card's arrays.

**The baton is deterministic.** Two runs of `pmhost.py` print the same
bytes. Only two loops needed a host yield: `pccmd_wait()`, which every
int 13h, PC-command and EtherDFS wait goes through, and
`dev_pmio_do_rw`'s transfer loop. Everything else that waits reads the
clock, and a clock read is a yield point.

**What it showed on its first day:**

- **The model was right where it was written from the firmware.** The card
  boots to READY in 2.0 ms of card time and the 2A3h test register counts.
  The ROM's 55AAh header is at D000:0 and the shared RAM's 12h magic at
  D400:0. Command 77h refuses the card's own line with 1, as `picomem.py`
  assumed. Every check of `sbfw` passes on the real firmware, CMS case
  included.
- **The SB needs the BIOS first.** The firmware answers 78h with
  `CMDERR_NOIRQ` (13h) until the PC's BIOS has run CMD_SendIRQ (30h) and
  written the line it saw into shared RAM byte 11 (`BV_IRQ`).
  `pmhost.Card.detect_irq()` is that step. It is POST's job, and on the
  iron it is always done before os8088 runs.
- **FINDING: the model's PORTUSED rule was wrong, and the real rule loses
  the card's CMS.**
  - `dev_sbdsp_install` (pm1x-pr, and FreddyVRetro's public `main`) MOVES
    an already-active DSP to any port it is asked for. It runs no
    `GetPortType` test on that path, so with CMS at 220h and the DSP at
    240h, `78h 220h` answers 0 and writes `DEV_SBDSP` over CMS's blocks.
  - Switching the DSP off then clears those blocks, and the card's CMS
    stops decoding: 224h reads FFh where it read 7Fh, until a reset.
  - `drivers/sound/picomem.inc`'s `pm_init` walks `pm_sbports` from 220h.
    So any `pm_init` while the DSP is on, with CMS enabled, takes 220h
    from CMS.
  - VIDSND's reconfigure does this, and it is the `vsfw` leg's 3 FAILs.
    SOUND.DRV re-attaching in the same session would too, since detach
    leaves the DSP on. That covers the Drivers page untick and re-tick, and
    a hibernate's detach and reload. This was reasoned from the code and
    not run.
  - `picomem.py`'s strict model answered PORTUSED there and could not see
    any of it.
  - **FIXED in the driver (2026-10-29), the owner's choice.** `pm_init`
    now sends `78h 0` before the walk (SPEC.md 34.10.2). That is 7 bytes:
    SOUND.DRV's image went 12,078 -> 12,085 and stayed a 12 KB claim. It
    is right on any firmware, the closed 2.x one included. `pmlib` is
    green, and the walk now reads `000, 220 (PORTUSED), 240`. The firmware
    fork's move path is unchanged, so the defect is still the card's.

### 6.2 Wave 1, as built

`tools/86box/patches/07-picomem.patch` adds the card to 86Box as an ISA
memory card, `isamem0_type = picomem`. It is ~330 lines of adapter, and it
`dlopen`s `libpmhost`. Two new library calls, `pm_io_map` and `pm_mem_map`,
tell it what the firmware decodes right now. It mirrors exactly that, per
8-port block and per 8 KB block, and re-reads both after every write the
card decoded and every 100 us tick, so a port or window appears and vanishes
as the firmware's table does. `tools/os88box.py` resolves the profile's
`lib` path and copies its `sd_image` into the run, as it already does for
disks. `vm/xt-picomem1` is a 640 KB 8088 XT with an OAK VGA and the card on
IRQ 5.

**What it showed:**
- The real PicoMEM BIOS comes up in 86Box: *"ROM @D000 - Port : 2A0 -
  IRQ : 5"*, with the IRQ found by the BIOS's own detection.
- os8088 boots to a desktop on `build/os8088-360.img`.
- On `make picomemtest`'s `pmsys360.img`, the Drivers page reads **Sound:
  Loaded**, attached to the card.

`OS88_PMHOST_LOG=1` puts each command in `86box.log`, and that log is a
complete trace of the PC's side of the card's protocol:
- the BIOS's RAM probe (0Bh x4);
- the IRQ detection (30h, 31h);
- the 64-block memory map (06h);
- SaveCfg, MEM_Init, DEV_Init and the disk list (08h, 07h, 09h, 85h, 82h);
- then os8088: SOUND.DRV's `78h 0` (SPEC.md 34.10.2's off-first), `73h 1`,
  `77h 0107h`, `78h 220h`, and USBMOUSE.DRV's `52h`.

Ethernet stays *Not loaded* until wave 4 builds the NE2000.

### 6.3 Wave 2, as built

**What exists.**
- **The card's DAC is the firmware's own mixer.** The I2S driver
  (`audio_i2s.c`) is replaced by an alarm in card time on its exact cadence:
  64 frames per buffer at 49,716 Hz, taken with `take_mixed_buffer()` as the
  DMA interrupt does.
- `pm_audio_read()` drains what the DAC "played". The 86Box adapter mixes
  it in, linearly resampled to 48 kHz.
- `OS88_PMWAV=FILE` writes it as a WAV in card time, so a capture is the
  same on every run.
- `tests/pmbox.py` (row `pmbox`, soak, **53 s**) boots `pmsys360.img` with
  `pmboxb360.img` in B: through `os88ui` under `$OS88_EMU=86box`.

**What it showed:**
- **The 1.x's virtual DMA works end to end on the real BIOS and
  firmware.** PMDMA's four ways of programming channel 1 (sb.inc's
  sequence, reads between, PMINIT's order, the page first) all copy from
  the address programmed. That covers the 8237 snoop, the multiplexer IRQ,
  and the PicoMEM BIOS's copy into the card's buffer.
- **SBTEST's tone comes out of the card.** It is a 1 kHz square at 8,000 Hz
  through `OSAPI_SND_STREAM`. The capture holds one 2.01 s stretch at
  995 Hz, 11,264 peak to peak, played by the firmware's DSP into its mixer.
- **Broken on purpose** (`OS88_PMHOST_NOSNOOP=1`, the card deaf to the 8237):
  - A to D still read right, because the BIOS's copy reads its source off
    the 8237 itself.
  - PMDMA's auto-init stream never ends, so its window never closes.
  - The tone never plays.
  - 4 FAILs. So the snoop is what keeps a STREAM going, not what aims a
    single transfer. That was not obvious from either side's source.

**FINDING (the firmware's, for FreddyVRetro): a PicoMEM with NO microSD card
takes away the PC's floppy drives.**
- `CMD_FDD_Mount` runs `pm_disk_update_fdd_nb()` only inside
  `if (PM_EnableSD())`. With no card, `New_FloppyNB` stays 0.
- The BIOS's `FDD_UpdateBIOSVar` then clears bit 0 and bits 6-7 of the
  equipment word.
- Measured: 0040:0010 reads 446Dh without the card and **442Ch** with it
  and no SD. os8088 then has no drive zone for A: or B:, though it booted
  from A:.
- The same card with an empty SD reads 446Dh. Every `vm/` profile
  therefore mounts one: `tools/picomem/mksd.py` writes it, and
  `build.sh` makes `build/picomem/sd.img`.

### 6.4 Wave 3, as built

**What exists.**
- **`tools/picomem/mksd.py` writes the card's whole SD.** `--fdd N:IMG` and
  `--hdd N:IMG` put an image in `/FLOPPY` or `/HDD` and name it in a
  `PICOMEM.CFG`, the binary `PMCFG_t` the BIOS's setup menu would have
  written after a user picked it. `--cfg FIELD=VALUE` sets any byte or word
  field.
- The layout is `PMCFG_t`'s, 644 bytes. `--selfcheck` compares every offset
  with `pm_cfg_fields()`, which the library builds from `offsetof()` on the
  firmware's own struct, and they agree.
- `OS88_PMSD=IMG` hands `os88box` a row's own card.
- `vm/xt-picomem1-1fd` is the same XT with ONE floppy drive.

**What `tests/pmbox.py` shows** (three boots, **122 s**, 13 checks):
- **fdd.** On the one-drive XT, B: exists and lists the card's image
  (PMDMA, SBTEST). This is SPEC.md 18.97.6 in the very situation it was
  written for: the PicoMEM BIOS counts the image into the equipment word,
  takes int 13h drive 1, and the kernel keeps a B: whose drive its probe
  cannot find. The listing is the card's firmware answering through its PC
  command channel: command 88h, the shared disk buffer, the BIOS's
  `REP MOVSW`.
- **Restart** is the warm reset of SPEC.md 18.100.1. It re-runs the card's
  BIOS (DEV_Init 09h, 1 -> 2), os8088 comes back, and B: is still the card's.
  On a warm boot the BIOS keeps the IRQ it found at power-on and skips
  command 30h, which the first draft of the check assumed it would not.
- **hdd.** `build/pmhd.img`, a bootable os8088 FAT16 disk at the card's
  default geometry (65/16/63), is HDD 0. With **A: empty**, os8088 reaches
  a desktop and C: is that disk. The kernel arrives in 24 int 13h calls,
  because the loader reads in long runs. That is the field machine's
  PicoMEM-hosted VHD of SPEC.md 52.10.16. It needs the card's own boot
  strap (`PMBOOT=1`): the XT's BIOS goes to ROM BASIC when its floppy is
  empty, as it did here first.

**Not yet broken on purpose.** The fdd and restart checks have no knob that
undoes them: there is no build without `dsk_fdd_pmemu` or with an `int 19h`
Restart. The snd leg's NOSNOOP break turns the row red as a whole.

### 6.5 Wave 4, as built

**What exists.**
- **The library compiles the card's NE2000 as-is** (`src/ne2000/ne2000.c`,
  `PM_WIFI=1`, the Pico W's).
- Its wire, the CYW43 Wi-Fi chip, is `pmhost_wifi.cpp`:
  - joining succeeds at once and the link is up;
  - `cyw43_send_ethernet` is the host's new `net_tx`;
  - `pm_net_rx` is the chip's receive callback.
- The station address is fixed: 28:CD:C1:08:80:88, Raspberry Pi's OUI.
- **In 86Box, a companion NIC carries the frames.** `network_attach` works
  only for a card 86Box's network layer itself starts, so patch 07 adds
  `picomem_ne2k`. The profile names it (`net_01_card = picomem_ne2k` on
  slirp), and it passes frames between that backend and the PicoMEM device.
- `mksd.py` writes a `WIFI.TXT`, because without one the firmware never
  starts the NE2000.
- The default card (`build/picomem/sd.img`) has the NE2000 at 300h on
  IRQ 3, as the field cards do.

**What it showed.**
- `tests/pmbox.py net`: ETHER.DRV attaches on the card at 300h with its
  station address, through SPEC.md 72.2's un-doubled-PROM probe. DHCP
  binds 10.0.2.15 through the card's 8390, with 2 frames each way.
- The NE2000's IRQ comes through the 1.x multiplexer as a software
  `int 0Bh`.
- The row is 16 checks over four boots, **143 s**.

**A host-only bug worth knowing:** `ne2000.c` calls newlib's `itoa`, which
glibc lacks. Without a prototype, C takes its result as an `int`, which
truncates a 64-bit pointer, and 86Box crashed the first time
`PM_Wifi_GetStatus` ran. The shim now declares it. This is the second time
the firmware's 32-bit assumptions have mattered (6.1's `pmh_cell` was the
first), so a new firmware file brought into the build wants its pointer
arithmetic read.

### 6.6 Wave 5, as built

**What exists.**
- **`pm_mouse_move(dx, dy, buttons)`** in the library is the card's USB
  host's output: it fills the firmware's own `pm_mouse` as a HID report
  would. `patches/host/` adds the main-loop block that sends it, which is
  the `USE_USBHOST` block's body without `tuh_task()` (the host build has
  no TinyUSB). One deliberate difference from a HID report: a movement
  that arrives before the last was sent is ADDED to it rather than
  replacing it, so a test that moves twice in one slice loses nothing.
- **In 86Box, `usb_mouse = 1`** makes 86Box's pointer the card's mouse,
  polled every 8 ms (a full-speed HID mouse's interval). The machine must
  have no mouse of its own, or it takes every movement first; the backend
  (`tools/os88box86api.py`) sets `mouse_type = none` for a profile that
  asks for the card's. `vm/xt-picomem1-usbm` is that profile.
- **The card's EMS needs no adapter code.** Its LTEMS port decode and its
  frame in PSRAM are the firmware's own, mirrored like any other block.
- **`mksd.py --cfg-from SDIMG`** starts PICOMEM.CFG from a card's saved
  one, which is what EMS needs (below).

**What it showed.**
- `tests/pmbox.py mouse` (check 9): with no 86Box mouse at all,
  USBMOUSE.DRV's PicoMEM backend turns the card's mouse on (52h), and
  os88ui reaches the A: zone and double-clicks it open. Every movement and
  both clicks went card IRQ -> the card's BIOS -> `int 33h` AX=0060h ->
  `um_pm33` -> `OSAPI_MOUSE_FEED`.
- **FIELD-NOTES 66's freeze is reproduced, exactly as the field saw it.**
  The leg moves the mouse BEFORE the machine runs, so the card holds a
  report from before os8088 started. With the driver built in the old
  order (52h's enable before the `int 33h` hook), the boot never reaches
  the desktop. With the same build and no early movement, it does. With
  the shipped order it does either way. So the defect needs both halves,
  the order and a report already waiting, which is what the field note
  says and what no model of the card had shown.
- `tests/ems.py --leg card`: EMS.DRV finds the card at 288h with its frame
  at E000h, and every check of the board leg holds at **256 pages**, 4 MB:
  the card's page register is a byte.

**Two things about EMS a user of the card would meet too.**
- **E000h, not D000h.** This firmware's BIOS sits at D000h (`ROM_BIOS`),
  so a 64 KB frame there would sit on it.
- **EMS set on a fresh config is turned off again.** At every boot the
  card's BIOS compares the memory map it detects with the one saved in
  PICOMEM.CFG ("Memory change : Reset the RAM Config."), and on any
  difference it resets the RAM settings, EMS among them. A config written
  from scratch has no map, so it always differs. The leg therefore boots
  once on a blank card, lets the BIOS save its map (08h), and sets EMS on
  THAT config, which is what a user does in the card's setup. MEASURED:
  with EMS set on a blank config, EMS.DRV answers DRVE_HW.

### 6.7 Wave 6, as built

**What exists.**
- **`libpmhost-v2`**: the same source built `PICOMEM_20`, with the
  firmware's own board block (`BOARD_PM20`, `MUX_V2`, `USE_HWIRQ`,
  `USE_HWDMA`, `PSRAM_HEAP`, the RP2350's 360 MHz) and `USE_GUS=0
  USE_MPU=0`. It links `pm2bios.bin`, the 2.x's BIOS. `build.sh` builds
  both boards by default. The 2.x's OLED is off in the host build, a
  display nobody can see; its I2C RTC and Qwiic bus answer as empty.
- **The bus header serves both multiplexers.** On a 2.x the PSRAM is
  mapped into the RP2350's address space, so every memory index, EMS
  included, reads through `RAM_Offset_Table`; the host's PSRAM is an 8 MB
  array at `__psram_start__`. The I/O half needed nothing: `GetPortType`
  already hides MUX_V2's A8-A9 inversion.
- **Four IRQ pins.** IRQ 3, 5, 6 and 7 each reach their ISA line only
  when jumpered (`pm_cfg_t.irq_lines`); the multiplexer's pin is IRQ 7's.
- **Real DMA.** The 2.x's DMA is a PIO program (`isa_dma_w_tc`): a word
  put to it raises DRQ, the 8237's cycle latches the bus byte, and the
  FIFO's interrupt runs the device's ISR. The host keeps both ends: a put
  is the new `host.dma_req` callback, `pm_dma_byte(v, tc)` is the cycle,
  and interrupt handlers are now kept so it can call the ISR. In 86Box a
  DRQ is counted on the card thread and answered after the slice with
  `dma_channel_read`, because 86Box's 8237 is pulled, not pushed.
- **Machines:** `vm/xt-picomem2`, `-1fd` and `-usbm`, jumpered as the
  field 2.x is (IRQ 7, DRQ 1). Section 4.4 named a `pc5150-picomem2`; the
  XT twin was chosen so every leg of `pmbox` runs on both boards
  unchanged. A 5150 profile can follow if a ROM-specific question needs
  one.

**What it showed.** This is the first run of `pm1x-pr` as a 2.x anywhere.
- `tests/pmbox.py --board 2`: every leg passes. SOUND.DRV's sequence is
  accepted on our firmware's 2.x build, **with the SB on IRQ 5, which
  this card has no jumper for**: the firmware sends it through the
  multiplexer on IRQ 7. SBTEST's 1 kHz tone plays 2.01 s through the
  card's real DMA. B: on the card, Restart, the hard-disk boot, DHCP
  through the NE2000 (IRQ 3, also through the multiplexer) and the USB
  mouse behave as on the 1.x.
- `tests/ems.py --leg card2`: EMS at 288h/E000h, 256 pages, out of the
  mapped PSRAM.
- `tests/picomem.py sb2fw` (host only): with lines 3, 5 and 7 jumpered, a
  64-byte transfer moves a DRQ a byte and ends on IRQ 5's own line.
- The 2.x's BIOS detects only the jumpered lines (`BV_HWIRQ 80` with IRQ
  7 alone, read off the firmware's own trace).
- **PMDMA has no answer on a 2.x.** It asks where the 1.x's VIRTUAL DMA
  copied from, and a 2.x copies nothing, so its G and E stages loop. The
  2.x leg leaves it out; SBTEST's tone is the 2.x's DMA test.
- MEASURED broken on purpose: the card's DMA jumper on 3 while SOUND.DRV
  programs channel 1 gives 0.01 s of tone, which is FIELD-MACHINES'
  "DMA jumper must be on 1" as a failure an emulator can show.

**Not done: the `.uf2`.** Section 4.4 said wave 6 "yields a `.uf2` to try
on the 5150". Building one needs the Pico SDK and an ARM toolchain, which
this tree's tools do not fetch; section 10 is the recipe. The host build
answers what our firmware would do on a 2.x; whether the iron agrees is
that `.uf2`'s question.

### 6.8 Wave 7, as built

**What exists.** `vm/at286-picomem1` is `vm/at286-port`, the mr286 every
kern_mega row on 86Box boots (with its seeded CMOS), plus a PicoMEM 1.x
whose USB mouse is the machine's only pointer, as the Victory 286 has one.
`tests/pmbox.py mega` (check 10) boots `build/mega.img` with SOUND, ETHER
and USBMOUSE wanted.

**What it showed.** Everything the card raises is its option ROM's IRQ,
so kern_mega reflects it to real mode (`mega_irqrom`). Two things ride
those reflections, and both work against the card's own firmware:
- **The mouse.** The card's ROM does `int 33h AX=0060h` inside the
  reflection. `mega_capon` catches it and `mega_m33go` replays it in
  protected mode (KERN-MEGA-PLAN 14.117, until now run only against a
  model ROM under Bochs). The card's mouse drives os88ui to B: and opens
  SBTEST. The mouse is moved before the machine runs, as in check 9.
- **The Sound Blaster.** SBTEST's tone plays 2.01 s, and every 128-byte
  block of it is a reflected multiplexer IRQ whose ROM handler copies it.
- MEASURED broken on purpose: with `mega_capon`'s int 33h capture skipped,
  kern_mega still boots in protected mode and SOUND.DRV still attaches,
  but the pointer never moves and the leg FAILS 3 checks.

The plan's "counted under `faults`/E9" is not what the leg asserts. The
reflections are proved by what they carry, the copies and the reports,
which is the stronger statement. A count would need a counter kern_mega
does not keep.

### 6.9 Wave 8, as built

**What exists.** `tools/martypc/patches/17-picomem.patch` adds
`devices/picomem.rs`, the same library as a MartyPC card under a
`[machine.picomem]` key (`irq`, and for a 2.x `irq_lines` and `dma`):
- `dlopen` by hand (`dlopen`/`dlsym` declared, glibc already linked), so
  the patch adds no crate.
- The card answers a port no other device claims and hears every write.
  MartyPC's port map has an "unresolved" fall-through, so that is two
  small hooks in `bus/io.rs`, and it is exactly how a 1.x snoops the 8237.
  Section 5.2's "snoop hook" is this.
- One MMIO window, D0000-EFFFF (`MmioDeviceType::PicoMem`), charged the
  firmware's own wait states: `pm_mem_waits`, times three system ticks a
  clock.
- Its `run` slice advances card time, answers a 2.x's DRQs from the 8237
  (`check_dma_ready`, `do_dma_read_u8`) and mirrors IRQ levels into the
  PIC.
- `os88marty.launch` gives a `_pm1`/`_pm2` machine `$OS88_PMHOST` for that
  board and a private copy of the microSD.
- Machines: `os8088_5150_herc_pm1_gla` (a 1.x on IRQ 5) and
  `os8088_5150_herc_pm2_gla` (a 2.x, IRQ 7 and DRQ 1, as 5150 #2's).

**One thing it cost.** The first build advanced the card on every `run`
call, and MartyPC calls `run` far more often than 86Box ticks. Each
advance hands the card thread the baton, so the guest ran at under a tenth
of real time. Card time now moves in 100 us slices, as in 86Box. A write
the card decodes still advances it at once. The boot-and-play went from
over 15 minutes to 33 s.

**What it showed** (`tests/pmmarty.py`, 68 s for both boards):
- On a 4.77 MHz 5150, both boards run the card's BIOS POST, SOUND.DRV
  attaches in 34.10's order, and SBTEST's tone plays 2.01 s out of the
  card's DAC. On the 1.x the 8088 copies every block in the card's ROM; on
  the 2.x the 8237 moves it.
- **`pm_runaway` did not fire on either board** (`sbl_drqmask` 0). Its
  32-byte threshold was set on the field 286. This is the first time it
  has met a card's own DMA pacing on the 8088 the project is calibrated
  against, and a false mask would have stopped the 2.x's stream.
- MEASURED broken on purpose: `OS88_PMHOST_NOSNOOP=1` silences the 1.x,
  and the 2.x, with no snoop to lose, plays on.

**What it does not answer.** The field's racing channel 1 came from the
1.x's board (a floating DRQ1, 34.10.5), which is below the seam. So this
shows that `pm_runaway` stays quiet when it should, not that it fires when
it should; `tests/picomem.py`'s racing case is still the only test of the
firing half. The 8 wait states charged for the 1.x's PSRAM are the cost
table's estimate (4.3), so `videmsslow`'s premise is now *run* against a
card model, but the number itself still comes from the field report.

## 7. What it will catch, and what it cannot

This is an assessment against FIELD-NOTES 66's list. Section 6 is where
each claim gets tested.

| 66 item | catchable here? |
|---|---|
| NE2000 ring at the wrong page; un-doubled PROM | **Yes**, wave 4 (86Box) |
| Empty slot accepted as an NE2000 | Yes, as a negative: a profile without the card |
| IRQ discovery stealing the multiplexer vector | **Yes**, wave 2 |
| 2.x refusing the SB until IRQ 7 | **Answered for our firmware**, wave 6 (6.7): it accepts the SB on an unjumpered line. The refusal was the closed firmware's |
| CMS holding 220h (PORTUSED) | **Yes**, wave 2 with CMS on in `config.txt` |
| B: missing; restart hanging | **Yes**, wave 3 |
| Mouse enabled before the hook froze the machine | **Yes, reproduced**, wave 5 (6.6) |
| Detach losing BX | Yes, wave 5 |
| **DMA cycle decoded as an 8237 write (A10)** | **No.** It is below the seam, in the PIO bus decode. |
| Channel 1 racing (`pm_runaway`) | **Partly.** The logic is the firmware's, but the pacing is section 4.3's cost table. MartyPC is where it is worth asking. |
| Disk starving the SB; crackles from IRQ-before-copy | **Yes in logic.** Both were firmware sequencing, which is exactly what is compiled. |
| Disk dropping sectors past 120 ms | Yes, if the cost table makes a copy that slow; a test can force it |

**Always out of reach:**

- PIO and bus decode
- IOCHRDY electrical timing
- the 1.x's shared SD/PSRAM SPI bus stalls
- Wi-Fi itself (slirp stands in for it)
- USB enumeration
- anything a specific PC's chipset does to an 8-bit card, which is REDLINE's
  subject (SPEC.md 103.6)

**The iron stays the authority**; this moves the first round of each
question off it.

## 8. The rows (BUILT)

The proposals this section carried became fewer, broader rows:

| row | emulator | asserts |
|---|---|---|
| `pmlib` | host (unicorn) | `tests/picomem.py`'s sb and vs legs against `libpmhost` instead of the Python model; the 2.x's real DMA (`sb2fw`) |
| `pmbox` | 86Box | the 1.x: BIOS, SOUND.DRV's order, PMDMA, SBTEST's tone; B: on the card and Restart; the hard-disk boot; the NE2000 and DHCP; the USB mouse; kern_mega with the card |
| `pmbox2` | 86Box | the same legs on a 2.x (PMDMA and kern_mega left out) |
| `emscard`, `emscard2` | 86Box | EMS.DRV against the card's EMS |
| `pmmarty` | MartyPC | both boards on a 4.77 MHz 5150; `pm_runaway` against the card's own pacing |

Each was broken on purpose and went red (docs/WRITING-TESTS.md 1): the
section above each wave's write-up names how. `box-pmmouse`'s proposed break
(the hook order reversed) is check 9's, and it reproduced FIELD-NOTES 66's
freeze. `box-pmsb1`'s (`NOPICOMEM=1`) was not used: `pmbox` refuses to
build its disks that way, so the snoop and the jumpers are the breaks.
`pmrace` became `pmmarty`, and it shows the quiet half only (6.9).

## 9. Decisions, and what is still open

**Decided by the owner, 2026-10-29:**

1. **Where it lives: os8088, `tools/picomem/`** (section 4.1). It is for our
   tools and soaks, and it may be packaged separately at the end.
2. **Which code is "the card": our own.** `pm1x-pr` (`5e507be2`), carried
   as a patch stack on upstream's newest public source, for **both** boards
   (section 4.4). **86Box is the primary target.**
   - The patch stack is how the tree keeps up with upstream: move the pin,
     re-cut the patches.
   - `os8088-pm114` (`8ab70ed`) is the same firmware plus the `OS88_DIAG`
     knob. It builds identically with the knob off, and the knob was never
     part of the upstream PR branch.
3. **GUS and MPU-401: out.** They are `USE_GUS`/`USE_MPU` in the firmware's
   own CMake, so leaving them out costs nothing. They can be turned on the
   day a package wants either (section 4.4).

**Found while answering 2: the October release cannot be built, and it
does not need to be.** FreddyVRetro has said the public firmware is not
the current code, and the history agrees:

- Upstream's **`a7cd888` "October 4 2026 Release"** adds
  `firmware/PicoMEM2/PM2-10-4-26.uf2`, but **no firmware source**. Its
  whole diff to `src/` is the GLaTICK ROM coming out.
- The newest firmware source anywhere public is still **`cfa8674`
  "PicoMEM 2 Firmware Source (21-6-2026)"**, plus one contributor's ADPCM
  pull request (`6faace6`).
- `pm1x-pr` sits on `be7b2e6`, which is upstream's HEAD, so **our patches
  are already on the newest source there is**.

What `PM2-10-4-26` changed and we cannot see, from its own release notes:

- CD-ROM (MKE) and ROM emulation
- the I/O dispatch moved into a table
- the PicoMEM IRQ configurable on any line
- SB DSP init timing
- RTC fixes for GLaTICK
- DFS fixes
- **"Sound Blaster IRQ/DMA config was not implemented (was using
  default)"**

The last one is FIELD-NOTES 66's 2.x item 4 seen from the other side. It
is the first thing wave 6 must check: does `pm1x-pr`'s command 77h work on
a `PICOMEM_20` build, or is the 2.x path in public source still the
June one?

**Decided by the owner, the same day:**

4. **The diagnostic commits are available as needed.** `os8088-pm114`'s
   `OS88_DIAG` work goes in as a `fw/` patch when a wave finds it useful
   (section 4.1). The likeliest wave is 2, where PMDMA's K, A and B keys
   could be checked off the iron.
   - It does **not** apply as-is. `os8088-pm114` and `pm1x-pr` are siblings
     on `be7b2e6`, not one on the other: each carries its own copy of the
     SB and disk-wait fixes. So the diag patch must be cut from the
     `OS88_DIAG` commits alone (`0a74dc4`, `bea75b4`, `056c4d5`) and
     re-applied on top of `pm1x-pr`.
5. **Licence: GPL-2, for the shim as well.** That matches the firmware it
   compiles and the library it builds.
6. **Purpose: our own testing, first and only.** Unlike the firmware fixes
   on `pm1x-pr`, which have gone to FreddyVRetro for review, this may never
   leave os8088. Packaging it, or offering it upstream, is an option to
   weigh once it is usefully working for us and not before. Nothing in
   waves 0-9 is shaped around outside users.

**Nothing is open.** The next step is wave 0.

## 10. Building the real firmware (for reference)

- **CI recipe** (`.github/workflows/build.yml`): Ubuntu 22.04, pico-sdk
  2.1.1 plus pico-extras, arm-gcc 14.2.Rel1, recursive submodules, then
  `cmake src && cmake --build`. `src/CMakeLists.txt`'s VS Code header names
  SDK 2.2.0 and toolchain 15_2_Rel1.
- **Board selection is by editing `src/CMakeLists.txt`.** The 2.x default
  is `pimoroni_pico_plus2_rp2350` with `PICOMEM_BOARD "PICOMEM_20"`. The
  1.x is `pico_w` with `"PICOMEM"`, which is what
  `firmware/picomem1/v1board.patch` sets.
- **What can be built.**
  - Upstream's HEAD (`be7b2e6`) builds, and `pm1x-pr` is one commit on it.
  - **`PM2-10-4-26` cannot be built.** Its source was never published
    (section 9).
  - For the 1.x, whether the published `PM_W_11_16_25` can be rebuilt from
    some commit in the history has not been checked, and nothing here needs
    it.
  - The only 1.x image whose exact source is public is the one os8088
    already ships: `PM_W_10_16_26_OS8088.uf2`, built from `pm1x-pr`.
- **`os8088-pm114`'s `3f63220` carries a Linux build script and notes** for
  the 1.x. It is the recipe `PM_W_10_16_26_OS8088.uf2` was made with, and
  the place to start when wave 6 builds a real 2.x `.uf2`.
- **Submodules are not populated** in a fresh clone (tinyusb, rp2040-psram,
  tusb_xinput, WonderMCA). `src/PrepareTinyUSB.sh` fetches tinyusb's
  dependencies. The host build of section 4 needs **none** of them, which is
  part of why the seam is drawn where it is.
- **The PC BIOS** is NASM (`pmbios/M.bat`), but use the committed
  `src/rom/pm{,2}bios.bin` (section 2).
- **DOS drivers:** `drivers/` holds binaries (PMINIT, PMEMM, PMMOUSE,
  NE2000, PMDFS). Sources are in `src/drivers/` except the PMDFS client.
  The HDD zips in `images/` carry them, which makes a DOS-under-86Box
  cross-check (docs/DOS-DEBUGGING.md's method) available once wave 2 lands.
- **Upstream information** is on the project wiki
  (github.com/FreddyVRetro/ISA-PicoMEM/wiki). The README has no build steps.
