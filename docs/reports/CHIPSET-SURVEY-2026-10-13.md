# 286/386 AT chipsets: identification, bus-timing registers and data books

**A survey, true of its sources on 2026-10-13 and not maintained.** Two
research passes made for docs/plans/REDLINE-TUNE-PLAN.md, reproduced as they
were written. Their sources:
- 86Box's `src/chipset/` and `src/machine/` at commit 12b1c8f;
- MAME's `cs8221.cpp` and `wd7600.cpp`;
- web-search results.

**No data book was opened for either pass.** From the container that ran them,
bitsavers, its mirrors, archive.org, vogons, dosdays and theretroweb were all
unreachable, so:
- every register claim is the emulators', tagged (a) stated, (b) inferred or
  (c) unknown;
- every URL is a search hit, marked EXACT or UNVERIFIED.

86Box stores most of these timing bits without modelling what they do, so a
register's NAME is a lead and never a fact about its encoding.

---

# 286/386 ISA chipset survey: identification and bus-timing registers

Sources: the 86Box tree at `.../scratchpad/86box/src/chipset/` (paths below are relative to it), MAME `src/devices/machine/{cs8221,wd7600}.cpp` (fetched from raw.githubusercontent.com), and web-search snippets. Every datasheet host I tried was blocked: bitsavers, its mirrorservice/kuiper mirrors, archive.org, vogons, dosdays, wimsbios, idhw, textfiles, discmaster and minuszerodegrees. **I have not read a single datasheet.** Labels used below: **(a)** the source states it, **(b)** inferred from a name or comment, **(c)** unknown. "Modelled" means 86Box calls `cpu_set_isa_speed()`; otherwise it only stores bits.

## Read this before designing a probe: an index write to 22h is NOT harmless

- **Intel 82335** puts *direct* 16-bit registers at 22h, 24h, 26h, 28h, 2Ah, 2Ch and 2Eh, with no index port. On this chipset, writing an "index" to 22h overwrites the memory-controller register (shadow RAM, video RAM, ROM size). Bit 15 is a lock, and 86Box keeps it locked once set (`intel_82335.c:52, 124, 154-176`).
- **G2 GC100 (XT)**: port 22h *is* the wait-state register itself, with no index (`gc100.c:96-104, 224`).
- **The AT I/O map reserves 020-03Fh for the master 8259** (MAME's WD7600 decodes the PIC across the whole 20h-3Fh range, `wd7600.cpp:151`) **(b)**. On a board that decodes coarsely, 22h aliases 20h (ICW1/OCW2) and 23h aliases 21h (the IMR). The same logic applies elsewhere: 72h/73h alias the RTC (70h-7Fh, `wd7600.cpp:157`), A8h/A9h alias PIC2 (A0h-BFh), and F2h/F3h sit in the 287 range F0h-FFh.
- **Cyrix Cx486SLC/DLC CPUs** fit 386SX/DX boards and also use 22h/23h, at indices C0h-CFh and FEh/FFh (DIR0/DIR1) **(b, general knowledge, not in this tree)**.
- **Side-effecting READS:**
  - VLSI SCAMP and VL82C480: reading **EFh resets the CPU** and reading EEh enables A20 (`scamp.c:1012-1022`, `vl82c480.c:328-336`).
  - Headland: reading 1ECh with MAR bit 7 set auto-increments the MAR (`headland.c:470-474`).
- **Writes that reset the CPU:**
  - NEAT 60h bit 5 (`neat.c:729`)
  - OPTi 381/481 reg 00h bit 5 and 391 reg 20h bit 0 (`opti391.c:190-196, 207-212`)
  - ACC 2036 reg 31h bit 1 (`acc2036.c:256-264`)

## Per-chipset table

| Chipset (CPU) | Config access; readable? | Identification (ID register / expected value) | ISA timing registers | Datasheet | Verdict |
|---|---|---|---|---|---|
| **WD/Faraday FE3600** (FE3001/3010/3021/3031; 286/386SX) | 63h: speed select (b7 lock, b2 CLKHS, b3 DMA speed, b6 stop). 72h is the pointer and 73h the value: register number and value in bits 7, 6, 3, 2. **Write-only.** | None by I/O. 72h/73h alias CMOS on many boards. Must come from BIOS / board identity. | R3 = 8-bit memory and 8/16-bit I/O command delay. R4 = 8-bit wait states (2 PROCLKs each); R4=10 is recommended at 16 MHz (your data). | none found | Supportable now, only behind a non-I/O identification (BIOS ID) |
| **WD76C10** (FE3600 successor, 286/386SX) | 16-bit word I/O at xx72h: 1072h clock control, 1872h "FPU bus timing & power down", 2872h "RTC, PVGA, 80287 timing, disk CS", 3872h memory control, and others. Unlock by writing **DAh to F073h**; 86Box keeps separate 8-bit and 16-bit lock states (`wd76c10.c:10-12, 1058-1060, 1175-1177`). Readable once unlocked. Locked reads return FFFFh. | FC72h "Lock status" is always readable. In 86Box, **bit 15 toggles on every read** (`wd76c10.c:1290-1294`), which is a read-only probe candidate (b, emulator behaviour, unverified). 1072h resets to 1000h (`:1322`). | 1072h/1872h/2872h are stored only; no bit decode (c). | none found | With a datasheet |
| **C&T NEAT CS8221 / NEATsx CS8281** (286/386SX) | Index 22h, data 23h. The index is valid only for 60h-6Fh (MAME `cs8221.cpp:address_w`: `(idx&0x60)==0x60`). **MAME invalidates the index after each data read**, so write 22h before every 23h access. 86Box also returns the index on reads of 22h (`neat.c:981-989`). Readable. | 60h bits 7:6 = 82C211 revision (`neat.c:48-50`). 64h bit 7 = version (0 = 82C212), bits 6:5 = revision (`:86-91`). 86Box *fakes* revision 2 (b). No vendor ID byte. Fingerprint: 63h reads back mask 00h; the 60h-6Fh masks are at `:248-255`. | **61h (RA1, "Command Delay")**: b1:0 AT bus command delay, b3:2 AT 8-bit command delay, b5:4 AT 16-bit memory delay, b6 quick mode, b7 hold delay (`:52-62`, a by name, encoding c). **62h (RA2)**: b1:0 BCLK source (0 = CLK2IN/2, 1 = CLK2IN, 2 = ATCLK); b3:2 AT 8-bit wait states; b5:4 AT 16-bit wait states (`:64-82`). The wait-state encodings are 0 = 2 WS, 1 = 3, 2 = 4, and **"4 = 5", which cannot fit a 2-bit field**, so the 5-WS code is unreliable (probably 3, c). 86Box only stores these values. | *CS8221 NEAT Data Book 1987-10* on bitsavers (`components/chipsAndTech/CS8221_NEAT_Data_Book_198710.pdf`); host blocked here | With datasheet (fields known; verify encodings) |
| **C&T SCAT 82C235 / SCATsx 82C836** (286/386SX) | 22h/23h, readable up to 4Fh (SCAT) or 64h (SX); otherwise FFh (`scat.c:1249-1250`). | **40h = version: 01h or 04h (SCAT), 13h (SCATsx)** (`scat.c:1415-1431`). It is not in the writable set, so writes are ignored (`:1033-1100`). The file header says it was reworked from the 82C235 datasheet (a). | 01h "DMA wait state control", 41h "Clock control" (defaults 02h; SX 06h), 44h "Peripheral control" (`:35-38`). Names only; no bit decode (c). | not found | With datasheet; ID is good |
| **C&T CS8230** (386/AT) | 22h/23h. | 04h "82C301 ID/version" read with mask ~E3h; 08h "82C302 ID/Version" read with mask ~E0h (`cs8230.c:87-93`). 86Box never initialises them, so expected values are c. | 05h/06h are "82C301 registers" (c). | not found | Not feasible without datasheet |
| **C&T CS8220** (PC/AT) | Direct registers at A4h/A5h/ABh, readable (`cs8220.c:100-140`). Note that A4h lies inside PIC2's A0h-BFh. | None. | None modelled. | 1990 C&T short form catalog only | Not feasible |
| **Headland GC101/102/103, GC113, HT18, HT21** (G2 lineage; 286/386) | CR index at **1EDh** (absent on GC10x: 1EFh then always hits CR0), data at **1EFh**, 3-bit index CR0-CR6. 1ECh/1EEh are EMS. Readable. | **CR4 bits 7:4 = revision, read-only** (writes keep the high nibble, `headland.c:414-417, 654`): GC103/GC113 = 0, HT18A = 1, HT18B = 2, HT18C = 8, HT21C/D = 1, HT21E = 2 (`:63-71`). CRI at 1EDh reads back on parts that have it. HT12 is not modelled. | None decoded (c). | not found | With datasheet |
| **G2 GC100/GC100A** (XT; out of scope) | Direct 22h/23h/25h (GC100) or C2h/C3h/C5h-C7h (GC100A). | — | Reg 2: b4 internal memory WS, b3:2 external memory WS, b1:0 I/O WS (`gc100.c:96-102`, comment). | GC100 doc mentioned in header | Collision example only |
| **C&T 82C100** (XT; out of scope) | 22h/23h index 40h-4Ch, plus **direct 72h** and 7Eh/7Fh (`82c100.c:363-367`). | — | — | — | Shows 72h is used by more than FE3600 |
| **VLSI SCAMP VL82C311** (386SX) | Index **ECh**, data **EDh**; also EAh/EBh/E8h (EMS), F4h/F5h, F9h, FBh. Readable 00h-16h. | **Reg 00h = D6h** (`scamp.c:36, 46, 1045`). Touch ECh/EDh only (EFh read = reset). | Config 02h-16h are mostly memory map; no timing decode (c). | not found | With datasheet; ID is good |
| **OPTi 82C283** (386SX) | Index 22h, data **24h**; reg 01h via 23h. **The index resets to FFh after every 24h access** (`opti283.c:228-273`). Readable 10h-14h. | No ID register (c). | **14h bit 0: ATCLK = bus/6 (0) or bus/4 (1)**. Modelled with `cpu_set_isa_speed` (`:249-262`); bit 6 = reset-on-HLT. | not found | With datasheet |
| **OPTi 82C291** (386SX) | 22h/24h, regs 20h-2Ch, readable (`opti291.c:59-104`). | None. | Stored only (c). | not found | With datasheet |
| **OPTi 82C381/391** (386DX) | 22h/24h, index auto-invalidated. Write-side reset hazard. | None. | Not decoded (c). | not found | With datasheet |
| **OPTi 82C493/495SX** (386) | 22h/24h (+23h on 493); also E1h/E2h. | None. | **25h bits 1:0 ATCLK: 0 = /6, 1 = /4, 2 = /3, 3 = 2/5 of the bus clock**, modelled (`opti495.c:420-438`). | not found | With datasheet |
| **ACC 2036** (286) | Index **F2h**, data **F3h**; regs 20h-3Fh readable; reads of F2h return the index (`acc2036.c:212-226`). | None. | Not decoded (c). | not found | With datasheet |
| **ALi M1409** (386SX) | 22h/23h, **locked at reset**. Unlock by writing **14h then 09h to 23h**; writing index FFh re-locks. Locked reads return FFh (`ali1409.c:146-162, 282-292`). Header: *"no datasheet... reverse engineering"*. | None. | **06h bits 6:4 ISA clock divider: /2, /4, /8, reserved, /3, /6, /5, /10**, modelled (`:210-245`). 0Ch: "turbo in bit 4, bus speed in the rest" (b). | none exists (per header) | Not feasible safely |
| **ALi M1429** (486; brief) | 22h/23h; unlock with C5h written to index 03h. | — | 20h bits 2:0 bus clock: ATCLK2, CLK2IN/4, /5, /6, /8, /10, /12 (`ali1429.c:64-71, 213-241`). | — | Out of scope |
| **Symphony SL82C461 Haydn II** (386/486) | Index **A8h**, data **A9h**, inside PIC2's range. | Reg 00h bit 6 = external cache (b). | 02h bits 4:2 ISA clock: CLK2/3, /4, /5, 7.159 MHz, /6, ?, /2.5, /2 (modelled). 08h bits 3:2 I/O recovery: only 00 = 0 and 11 = 12 SYSCLK known (`sl82c461.c:11-33, 223-249`). Header: *"may be inaccurate"*. | not found | With datasheet |
| **SiS Rabbit 85C310/320/330** (386DX) | 22h/23h; index 83h is a two-step register (`sis_85c310.c:84-113`). | None. | None. | not found | Not feasible |
| **Compaq / GRiD 1520** | Proprietary memory-mapped (Compaq) or GRiD ports (turbo at 416h, `grid1520.c:61`). | Machine-specific. | — | — | Out of scope |

Not in this tree: Headland HT12/HT12A, Suntac, Zymos POACH, VLSI VL82C100-series 286 parts, and the Faraday FE3000A.

## Prior art

- **CTCHIPZ** (c't magazine, early 1990s): a DOS program driven by per-chipset register definition files (`*.CNF`). The search snippets say it takes a config file and optionally a register index and value, and they warn that timing changes can crash a machine or cause parity errors. Uwe Sieber is not confirmed as the author. The source is a vogons thread [vogons p=1157136](https://www.vogons.org/viewtopic.php?p=1157136).
- **A NEAT register tool** is mentioned on vogons, with no link [vogons p=1284663](https://www.vogons.org/viewtopic.php?p=1284663). A SCAT thread says no equivalent tool was known.
- **ISAWAIT** ([github.com/oerg866/isawait](https://github.com/oerg866/isawait)) tunes ISA I/O recovery on PIIX and SiS chipsets only, which is later than this survey's scope.
- **SPEEDSYS** and **setchip**: nothing found.

## The BIOS ID string as a safe identification route

- **Old AMI format (1986-1990)**, e.g. `DINT-1123-04990-K8`:
  - leading letter: D = diagnostics, S = Setup, E = Extended Setup;
  - next code: chipset or board, with **NET = C&T NEAT**, C&T = C&T 386, 286 = "standard 286, VLSI, G2 286", G23 = G2 386, SUN = Suntac, AMI = AMI 386 board;
  - trailing Kn = keyboard BIOS revision.
  - Sources are hobbyist tables, which the search summarised as "reliable for identification but not authoritative" ([idhw](http://www.idhw.com/textual/guide/inst_mobo_ami.html), [pcrebuilding](https://pcrebuilding.altervista.org/93/600/BIOS+STRING+BY+CHIPSET/AMI/AMI.html)).
  - **No Faraday/WD code was found.** The "286" code lumps several chipsets together, so it cannot identify an FE3600 on its own.
- **Hi-Flex AMI (late 1990 onward)**: `AB-CCCC-DDDDDD-EFGHIJKL-mmddyy-MMMMMMM-N`. DDDDDD is the manufacturer and MMMMMMM looks like a chipset/model code (inference from an example) ([techenclave](https://techenclave.com/threads/how-to-identify-a-motherboard-from-bios.10322)).
- **Award**: the first five characters encode the chipset ([hardwaresecrets](https://hardwaresecrets.com/deciphering-the-bios-serial-number/)). That scheme is from the 486/Pentium era; I found no 286-era Award table.
- **Phoenix**: nothing found.
- **Verdict on the ROM route:** a ROM scan is read-only and safe. It is decisive only where the code is chipset-specific (e.g. NET). For the FE3600, the practical safe route is an **allowlist of exact ROM signatures (string plus checksum or date) from known FE3600 boards**, collected from users, rather than any decoded field.

## Summary

- **Positive read-only IDs exist** in this tree for only three parts: SCAT (40h = 01h, 04h or 13h), SCAMP (00h = D6h) and Headland (CR4 high nibble). WD76C10's FC72h bit-15 toggle is an untested candidate.
- **ISA clock encodings are modelled** only for OPTi 283 and 493/495, ALi M1409 and Symphony SL82C461. Every one of those is reverse-engineered or "may be inaccurate", so verify on hardware before writing.
- **NEAT** has named fields but no trustworthy encodings; its data book exists on bitsavers, which is blocked here.
- **The FE3600 has no I/O identification.** Gate it on the BIOS ROM.

---

# 286/386SX chipset documentation survey (for a bus-tuning utility)

**How the URLs were checked.** From this container, bitsavers.org, every mirror of it, archive.org, theretroweb, dosdays, vogons and Wikipedia were all unreachable (DNS fails for both `curl` and WebFetch). So every URL below came out of a WebSearch result, and nothing was opened. Bitsavers mirrors keep bitsavers' own paths: `bitsavers.mirrorservice.org/<path>`, `ftp.mirrorservice.org/sites/www.bitsavers.org/<path>` and `kuiper.mirrorservice.org/sites/www.bitsavers.org/<path>` are all `http://bitsavers.org/<path>`. **"EXACT" means the full path appeared in a search result**, on bitsavers or a mirror, and I rewrote it to bitsavers.org. Anything else is marked UNVERIFIED. 86Box facts come from `src/chipset/` and `src/machine/` at commit 12b1c8f.

## (a) Chipset table

Key: B = `http://bitsavers.org/components/`. "Bus timing" means the AT bus clock divider, 8/16-bit I/O wait states and command delay.

| Chipset | Era | How common (rough, my judgement) | Data book URL(s) | Bus timing programmable? | 86Box |
|---|---|---|---|---|---|
| C&T NEAT CS8221 (82C211/212/215/206) | 286 (NEATsx 386SX) | very common | EXACT: B`chipsAndTech/CS8221_NEAT_Data_Book_198710.pdf`; also `https://dosdays.co.uk/media/c_and_t/NEAT/NEAT%20Chipset%20Product%20Details.txt` | **Yes.** 86Box `neat.c`: reg 61h holds the AT bus command delays (8-bit and 16-bit). Reg 62h holds the 8-bit/16-bit AT wait states (2–5) and the BCLK select. The data book's summary also says "user selectable AT bus clock" | yes (`neat.c`, `neat_sx`) |
| C&T SCAT 82C235 | 286 | very common | No data book found. 82C235 is listed in the catalogs: EXACT: B`chipsAndTech/1989_Chips_And_Technologies_Short_Form_Catalog.pdf` and B`chipsAndTech/1990_Chips_And_Technologies_Short_Form_Catalog.pdf` | Likely. 86Box `scat.c` has "DMA wait state control" (reg 01h) and "clock control" (reg 41h) | yes (`scat.c`) |
| C&T SCATsx 82C836 | 386SX | common | None found (UNVERIFIED; browse B`chipsAndTech/`) | likely, same register family as SCAT | yes (`scat_sx`) |
| C&T CS8220 (82C201/202/A203-205 + 206) | 286 | common (early) | None found; listed in the 1990 catalog above | unknown | yes (`cs8220.c`) |
| C&T CS8230 (82C301-306) | 386DX | moderate | EXACT: B`chipsAndTech/CS8230_AT386_Chipset_82C301_302_303_304_305_306_Prelim.pdf`. B`chipsAndTech/CS8230_CS8231_386AT_Chipsets_1989.pdf` was named in a listing summary (UNVERIFIED). Alternate host: `https://oldcomputers.dyndns.org/public/pub/rechner/siemens/pg750/386at_chipsets_1989.pdf` | unknown | yes (`cs8230.c`) |
| C&T 82C100 | XT (8086/V30), not AT | uncommon | `https://knm.org.uk/datasheets/82c100.pdf` (EXACT, non-bitsavers) | n/a (XT) | yes |
| VLSI SCAMP VL82C311/310/311L | 286/386SX | common late | EXACT: B`vti/pc/VTI_VL82C310_82C311_82C311L_Data_Manual_199201.pdf` | unknown, not modelled in 86Box `scamp.c` | yes (`scamp.c`) |
| VLSI SCAMP II VL82C315A / VL82C316+323 | 386SX | moderate | EXACT: B`vti/pc/VTI_VL82C315A_Data_Manual_199206.pdf`, B`vti/pc/VTI_VL82C316_VL82C323_SCAMP_II_199210.pdf` | unknown | **no** |
| VLSI VL82C100-104 (+106) | 286 | common | EXACT: B`vti/databook/1988_VTI_ASIC.pdf` (101A/102A/103/104), B`vti/databook/1989_VTI_Computer_Products.pdf`, B`vti/databook/1990_VTI_Computer_Products.pdf`. Archive.org: `https://archive.org/details/VTI_ComputerProducts_1989` (item id taken from `ia803002.us.archive.org/6/items/VTI_ComputerProducts_1989/VTI_ComputerProducts_1989_text.pdf`). 82C106: EXACT `http://bitsavers.org/pdf/dec/decpc/DECpc_433/datasheets/82C106.pdf` | dosdays says the VL82C101 handles "clock generation, wait states" (registers UNVERIFIED) | **no** (only the VL82C113 combo chip is modelled) |
| VLSI VL82C201-204 ("200 series") | 286 | common | Listed in the TOC of the 1990 VTI Computer Products book (above) | unknown | **no** |
| VLSI TOPCAT VL82C286/386 | 286/386 | uncommon | EXACT: B`vti/pc/VTI_TOPCAT_VL82C286_82C386_PC_AT_Chipset_Oct90.pdf` | unknown | **no** |
| Headland HT12/HT12A | 286 | very common (1990+) | None found. Browse `https://www.dosdays.co.uk/topics/Manufacturers/headland.php` (it links a GCK113 datasheet) and vogons `viewtopic.php?p=1315071` / `p=1315202` (the MR_Slug chipset list) | unknown | **no HT12 device** (`headland.c` has GC10x, GC113, HT18 A/B/C, HT21 C/D/E) |
| Headland HT18 / HT21 / HT22 | 286/386SX | common | None found. One vogons poster also could not find HT18/C | unknown (not visible in 86Box source) | HT18, HT21 yes; HT22 no |
| G2 GC101/GC102 (=Headland HT101/102), GC103, GC113 | 286 | moderate | None found. radiomuseum.org `tubes/tube_gc102b1a.html` cites the literature only | unknown | yes (`headland_gc10x`, `headland_gc113`) |
| WD/Faraday FE3600 (FE3001/3010/3021/3031) | 286/386SX | moderate (OEM boards) | EXACT: B`westernDigital/_dataSheets/FE3600.pdf`, `.../FE3001.pdf`, `.../FE3010.pdf`, `.../FE3031.pdf`; B`westernDigital/_dataBooks/1991_SystemLogic_Imaging_Storage/` `03_FE3010C.pdf`, `04_FE3021.pdf`, `05_FE3021A.pdf`, `06_FE3031.pdf`, `07_FE3031A.pdf`, `00_TOC.pdf` | unknown (the user's FE3001 datasheet is the place to check) | **no** |
| WD76C10 / WD76C20 | 286/386SX | moderate (Amstrad, OEM) | EXACT: B`westernDigital/_dataBooks/1991_SystemLogic_Imaging_Storage/16_WD75C10_76C10.pdf`, `.../17_WD76C20.pdf`; `.../1992_SystemLogic_Imaging_Storage/04_WD76C10A.pdf`, `.../05_WD76C20.pdf`; `.../1993_SystemLogic_Imaging_Storage/05-WD76C10A.pdf` (hyphen, as seen), `.../06_WD76C20.pdf`, `.../07_WD76C20ALV.pdf`. Also `https://www.retrodev.com/files/WD76C10A_system_controller.pdf` | unknown | yes (`wd76c10.c`) |
| OPTi 82C281/282 | 386SX | moderate | EXACT: B`opti/dataSheets/82C281_OPTi_Cache_Sx_AT_199108.pdf` | unknown | **no** |
| OPTi 82C283 | 386SX | common | EXACT: B`opti/dataSheets/82C283_386SX_System_Controller_199409.pdf` | **Yes.** 86Box `opti283.c`: reg 14h bit 0 picks the AT bus clock as busclk/6 or /4 | yes |
| OPTi 82C291 | 386SX | moderate | EXACT: B`opti/dataSheets/82C291_SXWB_PC_AT_Chipset_199410.pdf` | unknown | yes |
| OPTi 82C381/382 | 386DX | moderate | None found (UNVERIFIED; browse B`opti/dataSheets/`) | unknown | `opti381` device yes |
| OPTi 82C391/392 | 386DX | moderate | EXACT: B`opti/dataSheets/82C391_82C392_OPTi-386WB_PC_AT_Chipset_199012.pdf` | unknown | yes |
| OPTi brochure (all ISA/VL parts) | — | — | `https://archive.org/details/bitsavers_optibrochuhure1993_1065722` (from `ia801301.us.archive.org/.../ISA_VL_Chipset_Brochure_1993.pdf`) | — | — |
| ACC 2036 | 386SX | moderate | None found | unknown | yes (`acc2036.c`) |
| ACC 2046 | 386DX | moderate | None found | unknown | yes (in `acc2168.c`, "2046/2168") |
| ALi M1209 | 386SX | moderate | None found | unknown | **no** |
| ALi M1217 | 386SX/SLC | common late | No M1217 sheet found. The M6117D datasheet embeds an "M1217B chipset" (vogons says most of it applies): `https://dosdays.co.uk/media/ali/M6117D_Datasheet.pdf` | **Yes, on the M6117** (86Box `ali6117.c` reg 1Eh: ISA clock 7.16 MHz or PCLK2/1.5, /2, ...) | yes (`ali1217_device` in `ali6117.c`) |
| ALi M1409 | 386 | uncommon | None found | **Yes.** 86Box `ali1409.c` reg 06h bits 6-4: clock divider /2 /3 /4 /5 /6 /8 /10 | yes |
| ALi M1207 / M1219 | 286 / ? | rare | None found; existence not confirmed | unknown | no |
| Symphony SL82C361 / SL82C461 (Haydn) | 386SX / 386DX | moderate | None found. datasheets360 lists only the SL82C365 | **Yes on the SL82C461.** 86Box `sl82c461.c` reg 02h bits 4-2: AT clock CLK2/2, /2.5, /3, /4, /5, /6 or 7.159 MHz; reg 08h I/O recovery time | 461 yes, 361 no |
| Intel 82335 | 386SX | moderate | None found online | unknown | yes |
| Intel 82230/82231 (licensed ZyMOS POACH) | 286 | moderate | None online. The Manx catalogue says "no copies known to be online" for Intel 240017-001 (`https://vt100.net/manx/details/47,13783`) | unknown | no on 286 (86Box's Poach is the 486 `isa486c.c`) |
| Suntac ST62BC001-006 / ST62C201+203 | 286 | common (Taiwan clones) | None found. Browse `https://www.dosdays.co.uk/topics/Manufacturers/suntac.php` | unknown | **no** |
| TI TACT82000 (82301/302/303), TACT83000 | 286/386DX | uncommon | TI "AT chip set" datasheet (74 pp), unofficial copy: `https://www.alldatasheet.net/datasheet-pdf/pdf/117216/TI/TACT83441.html`; page `https://www.dosdays.co.uk/topics/Manufacturers/ti.php` | unknown | **no** |
| UMC UM82C230/231; UM82C210 (211/212/215) | 286 / 286-386SX | moderate | EXACT: B`umc/UM82C231.pdf`, B`umc/UM82C206.pdf`; listed: B`umc/UM82C212.pdf`; 386DX: B`umc/UM82C384.pdf`, B`umc/UM82C388.pdf` | unknown | **no** |
| Citygate (often ALD 93C305 on 386SX) | 286/386SX | moderate, budget boards | None found | unknown | no |
| Elite, Logicstar, PCChips, Kosen, Siemens, NEC, Toshiba, Samsung, Hyundai, Gold Star | various | — | No data books surfaced. Most are OEM ASICs or relabels; Hyundai/GoldStar/Samsung 286 boards in 86Box use SCAT or C&T PC/AT | unknown | n/a |

## (b) Other common 286/386 chipsets that 86Box does not emulate

86Box's 286/386 chipset set is: NEAT/NEATsx, SCAT/SCATsx, CS8220, CS8230, SCAMP, Headland GC10x/GC113/HT18/HT21, OPTi 283/291/381/391, ACC 2036/2046, ALi M1217/M1409, SL82C461, SiS 310/401/460/461/411, Intel 82335, WD76C10, IMS 8848 and Compaq's ASICs.

The gaps below are roughly ranked by how often they turn up on surviving boards. **The ranking is my judgement; no source ranked them.**

1. **Headland HT12/HT12A** (286), with no datasheet found.
2. **VLSI VL82C100/101-104 and VL82C201-204** (286). These are fully covered by the VTI databooks above.
3. **Suntac ST62 / ST62C201+203** (286), with no datasheet found.
4. **UMC UM82C230/231 and UM82C210** (286/386SX), with bitsavers sheets above.
5. **Faraday/WD FE3600** (286/386SX), with full bitsavers coverage.
6. **OPTi 82C281** (386SX cache), on bitsavers.
7. **ALi M1209** (386SX), with nothing found.
8. **Symphony SL82C361** (386SX), with nothing found.
9. **VLSI TOPCAT and SCAMP II** (VL82C315/316), on bitsavers.
10. **Intel 82230/82231**, **TI TACT82000** and **Headland HT22**: scarce documentation.

## (c) The three test machines

**Packard Bell PB286LP: the chipset is unidentified, and 86Box's `pb286` is not this machine.**

- 86Box `machine_table.c`: `"[ISA] Packard Bell PB286"`, internal name `pb286`, `.chipset = MACHINE_CHIPSET_DISCRETE`, alias `"Packard Bell IS/VT286"`.
  - BIOS: `roms/machines/pb286/LB_V332P.BIN` and `HB_V332P.BIN`.
  - Bus 10-12.5 MHz, RAM 256K-1MB.
  - `machine_at_pb286_init` is only `machine_at_ibm_common_init`. **No chipset device and no tuning registers are modelled**; it is an IBM-AT-style discrete board.
  - os8088's own FIELD-NOTES record it as "Packard Bell 286, BIOS 09/17/86".
- The real **PB286LP** is a different machine. Wikipedia "PB286LP" (search snippets only) calls it Packard Bell's first laptop, announced in 1988 and sold 1989-91. The snippets say the "systems chipset ... was manufactured by Intel, while the BIOS was provided by Award". No part number is given.
- `https://pb.retropc.se/286lp.html` describes the 286LP board:
  - CPU 80C286-12, with "software switches in setup for 6/12 MHz".
  - Award BIOS 3.03E, 1-5 MB RAM.
  - It does not name the chipset.
- **Inference, not confirmed:** Intel's only 286 AT chipset of that period was the 82230/82231 (licensed ZyMOS POACH), for which no datasheet is online.
- **Next step:** read the chip markings off the board. Related pages: `https://pb.retropc.se/286lpv.html` and `https://pb.retropc.se/286l.html`.

**Compaq Portable III: Compaq's own ASICs, with no published chipset registers found.**

- **86Box models no tuning registers here.**
  - `portableiii` is `MACHINE_CHIPSET_PROPRIETARY`, bus 8-12.5 MHz.
  - Its init adds `compaq_device`, whose whole `compaq.c` is a memory remap: 0xA0000-0xFFFFF is mirrored to 0xFA0000, plus `mem_remap_top(384)`. **It has no I/O ports at all.**
  - The KBC is plain `kbc_at_device` with params 0. `kbc_at.c` only documents Compaq input-port bits ("0 = Auto speed, 1 = High speed"; "Slow/Fast").
- **The software speed interface that is known:**
  - INT 16h AH=F0h sets the CPU speed: AL = 0 COMMON, 1 FAST, 2 HIGH, 3 toggle, 8 AUTO, 9 custom.
  - INT 16h AH=F1h reads it back.
  - Source: Ralf Brown's list as used by `https://github.com/indrekis/compaq_portable_III_conf` (cloned and read: `setmode_main.cpp` and `SetHIGH.ASM` = `mov ax,0F002h / int 16h`). Its README says numeric codes are ignored on the 286 Portable III and that `MODE SPEED=HIGH` works under SETVER.
- Seasip's Portable II page (`https://seasip.info/VintagePC/compaq2.html`):
  - It lists KBC commands for 6/8/16 MHz, a speed toggle and a speed-limit command.
  - It says the 106436-001 KBC is "most likely the same" as the Portable III's.
  - It says port 86h "may" be involved; the page itself marks this uncertain.
- The Portable III page is `https://seasip.info/VintagePC/compaq3.html`.
- A brochure lists a "COMPAQ PORTABLE III Technical Reference Guide" (`https://www.thecomputerarchive.com/archivemain/Computers/PC%20portables/Compaq%20Portable%20III.PDF`). **No scan of that guide was found**, and no port is documented anywhere for wait states or AT bus timing. This is CPU clock selection only.

**Packard Bell Victory 286 (FE3600): not confirmable from reachable sources.**

- `docs/FIELD-MACHINES.md` cites `theretroweb.com/motherboards/s/packard-bell-victory`. No search result indexed that page, and the site is blocked here.
- `https://pb.retropc.se/victory.html` lists an 80286-12, Phoenix BIOS and a Paradise SVGA chip with 256 KB. It names no chipset.
- `https://th99.classic-computing.de/m/P-R/31557.htm` (search snippet) gives "Chip Set: Unknown".
- The FE3600 attribution therefore rests on the owner's own board inspection. Its documentation is the WD files in the table.

## (d) Directories to browse where no exact file was confirmed

- `http://bitsavers.org/components/chipsAndTech/` — look for SCAT 82C235, SCATsx 82C836, CS8220, a CS8233 PEAK/386 book, and the `Schematics/` subfolder (a `NEAT_Chipset_Motherboard_Schematic_1987.pdf` was named in one listing).
- `http://bitsavers.org/components/vti/databook/` and `http://bitsavers.org/components/vti/pc/`
- `http://bitsavers.org/components/opti/dataSheets/` — 82C381/382, 82C206.
- `http://bitsavers.org/components/westernDigital/_dataSheets/` and `.../_dataBooks/1990_*`, `1991_*`, `1992_*`, `1993_SystemLogic_Imaging_Storage/`
- `http://bitsavers.org/components/umc/` — UM82C211/215; the 1986 UMC data book (~21 MB) is in this directory.
- `http://bitsavers.org/components/ali/` — only M1487/M1489 and M1621 were seen; M1209/M1217 are unconfirmed.
- `http://bitsavers.org/components/intel/` — 82335 and 82230/82231 (likely absent).
- `http://bitsavers.org/components/ti/` — TACT.
- No bitsavers folder was seen for `headland/`, `g2/`, `suntac/`, `acc/` or `symphony/`; these are UNVERIFIED.
- `https://dosdays.co.uk/topics/datasheets_index.php` — a datasheet archive with files under `dosdays.co.uk/media/...`.
- Its per-vendor pages: `https://dosdays.co.uk/topics/Manufacturers/{candt,headland,vlsi,suntac,ti,umc,ali}.php`.
- `https://www.dosdays.co.uk/topics/286_mobos.php`
- theretroweb chip and board pages: `https://theretroweb.com/chips/...` and `https://theretroweb.com/motherboards/...`. None were indexed by search; browse them by hand.
- Last resort: the 86Box sources (`neat.c`, `scat.c`, `opti283.c`, `sl82c461.c`, `ali1409.c`, `ali6117.c`, `headland.c`). They carry register descriptions taken from datasheets, and are useful where no datasheet is online.
