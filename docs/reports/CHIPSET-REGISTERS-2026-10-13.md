# Chipset register reference from the data books - 2026-10-13

A MEASUREMENT in this directory's sense (docs/README.md): what twenty-two
vendor data books say, read on 2026-10-13 for docs/plans/REDLINE-TUNE-PLAN.md
§5, and true of those books rather than of any tree. It is the source REDLINE's
chipset rows (SPEC.md 103.6, `apps/redline/csrows.inc`) cite, and the probe
rules in 103.6.1 were set from it.

**The books themselves are not in the repository** - 172 MB of third-party
scans, held by the fork owner. Every fact below carries `[BOOK pN]`, where N is
the PDF page and not the printed folio; a session that needs to re-read a page
asks the owner for the file. Three readers wrote the three parts below, each
with its own key table of book tags: A is Chips & Technologies, B VLSI, C
OPTi, UMC, Western Digital and ALi, and a cross-reference such as `B1.3`
names a section of that part.

Where a book and 86Box disagree, the book wins and the disagreement is noted
in place: 86Box is a reference for what an emulated board does, which is a
test fixture, and never for what a real one does
(docs/reports/CHIPSET-SURVEY-2026-10-13.md is the emulator-derived survey this
corrects).

The findings that decided the probe design, in one place:

- **The FE3600 is write-only** - FE3001: "All other I/O locations are write
  only" - so it is identified by its BOARD (the BIOS fingerprint), never by
  a port.
- **Port 22h is the 8259 on a board that decodes 20h-3Fh coarsely**, so an
  index byte written there is a PIC command. Only 40h (OCW2 no operation) and
  60h-67h (specific EOI on an idle level) are written by any probe; OPTi's
  10h-14h (ICW1 - a PIC reset), M6117D's 13h, CS8230's 10h-17h and 20h, and
  NEAT's 68h-6Fh (special mask mode, poll) are never written.
- **VLSI's ECh/EDh** answer a version byte at index 00h across SCAMP, SCAMP
  II and TOPCAT; EFh is never touched (a READ resets the CPU).
- **The WD76C10 and every OPTi part need a write before they say anything**
  that identifies them, so in REDLINE they are pick-only rows.
- **Bus timing that a 286 set does NOT expose**: VLSI's VL82C100/201 sets and
  UMC's UM82C230 are strapped, not programmable.


---

## Chips & Technologies AT chipsets: identification and bus-timing registers, from the data books

Extracted 2026-10-13 from the vendor books below. Every fact carries a citation of the form `[BOOK pN]`, where N is the **PDF page** (1-based). For NEAT, CS8230 and 82C100 I spot-checked that the PDF page equals the printed page number, and it did. Anything marked **UNKNOWN** is not in these books. Anything marked **DERIVED** is my inference, not a vendor statement.

| tag | file |
|---|---|
| NEAT | `datasheets/Data Sheets/CS8221_NEAT_Data_Book_198710.pdf` (90 pp, Oct 1987) |
| CS8230 | `datasheets/Data Sheets/CS8230_CS8231_386AT_Chipsets_1989.pdf` (220 pp) |
| SF89 | `datasheets/Data Sheets/1989_Chips_And_Technologies_Short_Form_Catalog.pdf` |
| SF90 | `datasheets2/Data Sheets 2/1990_Chips_And_Technologies_Short_Form_Catalog.pdf` |
| C100 | `datasheets/Data Sheets/82c100.pdf` |
| SURVEY | `docs/reports/CHIPSET-SURVEY-2026-10-13.md` (86Box/MAME-derived, not authoritative) |

**What the books cover.** Only NEAT (82C211/82C212), the 82C301/82C302/82C307 (CS8230/CS8231) and the 82C100 are documented to register level. The short-form catalogs carry **no register-level detail at all** for SCAT 82C235, CS8220, 82C206 or NEATsx. **SCATsx 82C836 does not appear in either catalog**; a grep for "836" finds nothing.

---

### A0. Cross-cutting: ports 22h/23h and boards without a chipset (applies to every section)

- All four documented C&T parts use **index port 22h** and **data port 23h**, and every 23h access must be preceded by a fresh write to 22h:
  - NEAT 82C211 and 82C212: "Every access to port 23H must be preceded by a write of the index value to port 22H even if the same register data is being accessed again" [NEAT p29, p46].
  - 82C301/82C302: [CS8230 p15–16]. 82C307: [CS8230 p163]. 82C100: [C100 p36].
  - The 82C100 states the reason: "To minimize accidental writes to these configuration registers, any access to location 23H must be preceeded by a write to location 22H" [C100 p36]. This supports MAME's index-invalidated-after-each-data-access model (SURVEY), but no book states what a 23h access *without* a fresh index does: **UNKNOWN**.
- The books themselves call these "I/O ports 22H and 23H normally found in the interrupt controller" [CS8230 p15, p44, p163; NEAT p46].
- **DERIVED (not in these books):** the IBM PC, XT and AT decode the master 8259 over the whole 20h–3Fh range. On a board without a C&T chipset that decodes the same way, a write to 22h is therefore a write to the PIC at A0=0 (ICW1/OCW2/OCW3), and a read of 23h returns the IMR. What each probe index would do as a PIC command:
  - **NEAT 60h–67h** = OCW2 *specific EOI* for IR0–IR7. This is harmless only if that level is not in service, i.e. never issue it from inside an ISR.
  - **NEAT 68h–6Bh** = OCW3 with ESMM=SMM=1, which **sets special mask mode**. **NEAT 6Ch–6Fh** = OCW3 with P=1, a **poll command**. Both are unsafe.
  - **CS8230 04h–06h** = OCW2 000 (rotate-in-AEOI clear).
  - **CS8230 08h–0Fh** = OCW3 with ESMM=0. Within that, 0Ch–0Fh are poll commands.
  - **CS8230 10h–17h** = **ICW1**, which reinitialises the master PIC: **catastrophic**. **20h** = non-specific EOI: **unsafe**.
  - **82C100 40h–47h** = OCW2 "no operation". **48h–4Bh** reset special mask mode. **4Ch–4Fh** = poll.

  A probe must therefore use only indices whose PIC meaning is benign, run with interrupts off and never from an ISR. It must also tell an IMR read-back apart from a register: compare each 23h read with a direct read of 21h.
- Intel 82335 boards put real registers at 22h (SURVEY), so writing the index port is itself a destructive write there. Nothing in these books addresses that. Exclude it first by other means (UNKNOWN from these sources).
- **I/O recovery (time between back-to-back I/O commands) has no register** in NEAT or the 82C301. The nearest control is NEAT RA1 bit 7 (A1.3). **NOT FOUND** as a dedicated field in any of these books.

---

### A1. C&T CS8221 NEAT (82C211 bus controller, 82C212 memory controller, 82C215 buffer, 82C206 IPC), 286 at 12–16 MHz

#### A1.1 Configuration access
- Index port 22h, data port 23h [NEAT p29, p46]. Every 23h access needs a fresh 22h write [NEAT p29, p46].
- Register map:
  - 82C211: RA0 = 60h "PROCCLK Selector", RA1 = 61h "Command Delay", RA2 = 62h "Wait State/BCLK Selector" (Table 1.3) [NEAT p29].
  - 82C212: RB0 = 64h Version, 65h ROM Configuration, 66h–69h Memory Enable-1..4, 6Ah Bank 0/1 Enable, 6Bh DRAM Configuration, 6Ch Bank 2/3 Enable, 6Dh EMS Base Address, 6Eh EMS Address Extension, 6Fh Miscellaneous (Table 2.2) [NEAT p46].
  - **63h is not assigned** in either table. Its behaviour, and the behaviour of any index outside 60h–6Fh, is **UNKNOWN** (the book says nothing about invalid indices).
  - The book is inconsistent about numbering: Table 2.2 calls 6Fh "RB11", the register page calls it "Miscellaneous Register RB12" [NEAT p46 vs p52].
- Readability:
  - 61h and 62h are R/W in every field [NEAT p30, p31].
  - 60h mixes field types: bits 7:6 R, bit 5 **W only**, bits 4 and 2 R/W, bit 0 R [NEAT p30].
  - 64h bits 7:5 are R [NEAT p47]. 66h–6Fh are R/W [NEAT p48–52].
  - What a **read of port 22h** returns is **UNKNOWN**: the book documents no index read-back (86Box returns the index; SURVEY).
- Hardware: the 82C212 asserts XDEN "during I/O accesses to locations 22H and 23H" so that its registers are reached over MA0–7 [NEAT p14, p15, p43].
- Reserved bits: "All reserved bits are set to zero by default and when written to, must be set to zero" (82C211) [NEAT p29]. The 82C212 says the same: "...set to zero by default and must be maintained that way during write operations" [NEAT p46].

#### A1.2 Identification
- **60h (RA0) bits 7:6** = "82C211 revision number. 00 is the initial number." Bits 3 and 1 are reserved (0). Bit 0 = READY timeout status (R) [NEAT p30].
- **64h (RB0) bit 7** = "NEAT memory controller identifier. 0 = 82C212". **Bits 6:5** = "82C212 revision number. 00 = initial revision number". **Bits 4–0 reserved** [NEAT p47].
- There is no vendor/ID byte. A positive fingerprint from documented facts only (**DERIVED**):
  - `64h & 9Fh == 00h` (bit 7 = 0 for 82C212, bits 4–0 reserved 0).
  - `60h & 0Ah == 00h` (reserved bits 3 and 1).
  - `62h & C0h == 00h` (bits 7:6 reserved) and `62h & 03h != 03h` (11 is "Reserved").
  - None of these values equal to FFh.
- Safest sequence (DERIVED): `pushf/cli`; `out 22h,64h`; `in al,23h`; `out 22h,62h`; `in al,23h`; `out 22h,60h`; `in al,23h`; read port 21h directly; `popf`.
  - Reject if any value is FFh, or if every 23h value equals the 21h value (PIC alias).
  - Use **only 60h–64h**: on an aliasing board 68h–6Fh become special-mask/poll commands (A0). 60h–64h are specific EOIs for IR0–IR4, harmless outside an ISR.
  - Reads of these registers have no documented side effect. Bit 0 of 60h is a status bit, but no read-to-clear is stated: **UNKNOWN**.
- NEATsx (CS8281: 82C811/82C812) and LeAPset (CS8223/CS8283: 82C241/82C841): the catalog says only "Based on the proven NEAT CHIPSet architecture" [SF90 p20] and "LeAPset internal registers are a superset of NEAT registers" [SF90 p13]. **Their ID values and register map are UNKNOWN** from these books.

#### A1.3 Bus timing

**RA0, 60h (82C211)** [NEAT p30]

| bit | meaning | default |
|---|---|---|
| 7:6 | 82C211 revision (R) | 00 = initial |
| 5 | "Alternate CPU reset. A low to high transition in this bit activates a CPU reset. Once active, it remains active for 16 PROCCLK cycles and then goes low." (W) | — |
| 4 | Processor clock select: 0 = PROCCLK = CLK2IN; 1 = PROCCLK = BCLK | 0 |
| 3 | reserved | 0 |
| 2 | Local bus READY timeout NMI enable: 1 enables, 0 disables | 0 |
| 1 | reserved | 0 |
| 0 | READY timeout occurred (R): "128 PROCCLK cycles after AF16 has been asserted" | — |

**RA1, 61h, "Command Delay"** [NEAT p30] (verbatim, bit pairs give 0–3 BCLK cycles)

| bits | field | default |
|---|---|---|
| 1:0 | "AT Bus I/O cycle command delay. Specifies between 0 to 3 BCLK cycle command delays for AT I/O cycles." | **1** |
| 3:2 | "AT Bus 8 bit memory command delay ... 0 to 3 BCLK cycle command delays for 8 bit AT memory cycles." | **1** |
| 5:4 | "AT Bus 16 bit memory command delay ... between 0 and 3 BCLK cycle command delays for 16 bit AT memory cycles." | **0** |
| 6 | "Quick mode enable. A zero enables Quick mode and a one disables it." | **1** (disabled) |
| 7 | "Address hold time delay. A one enables extra address bus hold time and a zero disables it." | **0** |

- Encoding is n = n BCLK cycles (00 = 0 … 11 = 3) [NEAT p30].
- Units: a command delay holds XMEMR/XMEMW/XIOR/XIOW back "from going active in BCLK steps" [NEAT p21].
- Defaults computed from the register page: **61h = 45h** (DERIVED from the bit defaults).

**RA2, 62h, "Wait States"** [NEAT p31] (verbatim)

| bits | field | encoding | default |
|---|---|---|---|
| 1:0 | Bus clock (BCLK) source | 00 BCLK = CLK2IN/2 · 01 BCLK = CLK2IN · 10 BCLK = ATCLK · 11 Reserved | 00 |
| 3:2 | "8 bit AT cycle wait state generation" | **00 2 WS · 01 3 WS · 10 4 WS · 11 5 WS** | "Default is 5" (= 11) |
| 5:4 | "16 bit AT cycle wait state generation" | **00 0 WS · 01 1 WS · 10 2 WS · 11 3 WS** | "Default is 3" (= 11) |
| 7:6 | reserved | — | 0 |

- Defaults computed from the register page: **62h = 3Ch** (DERIVED).
- Units: wait states delay READY "in steps of the AT command cycles (AT-TC)" [NEAT p21].
- The fields say "8 bit AT cycle" and "16 bit AT cycle", and there is **no separate I/O wait-state field**. Whether 8-bit I/O takes the 8-bit field is not stated explicitly: **UNKNOWN**, though the name implies all 8-bit AT cycles.
- **Contradiction inside the book.** The Normal-mode text says "If activated by default, I/O cycles will have one command delay, 8 bit AT memory cycles will have **4** wait states, 16 bit AT memory cycles will have **1** wait state" [NEAT p25]. The register page says 5 and 3 [NEAT p31]. Read the hardware back rather than assume either.
- **Contradiction with SURVEY / 86Box.** The 8-bit field is plain 2-bit binary, `11` = 5 WS [NEAT p31]. 86Box's "4 = 5 WS" cannot be right: code 4 does not exist, and the code for 5 WS is 3. The 16-bit field is 0–3 WS (00 = 0 WS), *not* 86Box's 2–5 style, if 86Box applies the same table to both. Neither table is consistent with a "0 = 2 WS" reading of the 16-bit field.

**Clocks** [NEAT p19–20]
- SYSCLK "is always BCLK/2". The 82C211 has two inputs, CLK2IN ("at a maximum of twice the processor clock") and ATCLK ("derived from a crystal ... typically ... lower frequency than CLK2IN").
- Defaults: "PROCCLK = CLK2IN, SYSCLK = CLK2IN/4" [NEAT p20].
- Modes [NEAT p25–28]:
  - **Normal** (default): PROCCLK = CLK2IN, BCLK = CLK2IN/2, SYSCLK = CLK2IN/4. ALE and commands only on AT cycles.
  - **Quick**: write 0 to REG61<6> *with* PROCCLK = BCLK = CLK2IN, SYSCLK = CLK2IN/2. ALE is issued on the AT bus for *every* cycle, local ones included. Described as "useful for high speed add-on cards".
  - **Delayed**: quick disabled, PROCCLK = CLK2IN, BCLK = CLK2IN, SYSCLK = CLK2IN/2. Local LA17–23 are latched by EALE. "useful for slow peripheral AT add-on cards".
  - **External** (asynchronous): BCLK = ATCLK, SYSCLK = ATCLK/2. PROCCLK is CLK2IN, or ATCLK if 60h bit 4 is set.
- Arithmetic (DERIVED): a 16 MHz 286 has CLK2IN = 32 MHz, so the default SYSCLK is 8 MHz, and BCLK = CLK2IN (code 01) gives SYSCLK = 16 MHz.

**Address hold / recovery.** "certain slow peripherals require between 50 to 60 nanoseconds between command going inactive to the next ALE going active, to provide sufficient data recovery time". RA1<7> delays IALE "by one Tc state" via READY. "This additional hold time can only be programmed in the Normal mode." [NEAT p26]. This is the book's only I/O-recovery-like control.

**Recommended values per CPU clock: NOT STATED.** The book gives the modes and defaults only.

**82C212, local-bus timing** (not ISA; listed because 6Bh shares the index space) [NEAT p50]
- **6Bh (RB7)**:
  - bits 1:0 ROM wait states: 00 = 0 · 01 = 1 · 10 = 2 · **11 = 3 (default)**.
  - bits 3:2 EMS wait states: 00 = 0 · 01 = 1 · **10 = 2 (default)** · 11 reserved.
  - bit 4 EMS enable (0 default). bit 5 RAM wait states (0 = 0 WS, **1 = 1 WS default**).
  - bit 6 640K–1M relocation (**1 default**). bit 7 page/interleave enable (0 default).

#### A1.4 Hazards
- **60h bit 5 resets the CPU** on a 0→1 transition, "after the current I/O command goes inactive", and RESET3 holds ≥16 PROCCLK [NEAT p19, p30]. Always write bit 5 = 0. Never read-modify-write 60h assuming the bit reads back: it is W-only, so its read value is **UNKNOWN**.
- **60h bit 4 = 1** puts the CPU on BCLK, i.e. slows it [NEAT p20, p30].
- **62h bits 1:0 = 10 (ATCLK)** with no ATCLK oscillator or crystal fitted. The pin "is only required if ... BCLK ... will not be derived from CLK2IN" [NEAT p5], so selecting it on a board without one leaves the AT state machine without a clock. **DERIVED: hang.** 11 is "Reserved" [NEAT p31].
- Quick and Delayed modes require BCLK = CLK2IN [NEAT p26, p27]. At 16 MHz that puts SYSCLK at 16 MHz (DERIVED).
- **6Fh bit 1 = 1 forces A20 = 0** [NEAT p52]. Other 82C212 registers remap or shadow memory, including **6Bh bit 6, which relocates 80000–9FFFFh** [NEAT p50]: do not touch them.
- Reserved bits must be written 0 [NEAT p29, p46].
- Persistence: RESET4 resets the 82C212's configuration registers [NEAT p12], and RESET4 is asserted only "as long as the Power Good signal is held low" [NEAT p19]. A warm reset (8042 RESET2, shutdown) asserts only RESET3 [NEAT p19]. **DERIVED:** a bad setting survives Ctrl-Alt-Del and a keyboard-controller reset until something reprograms it. That the 82C211's own registers are reset by RESET4 is likewise **UNKNOWN**.

#### A1.5 BIOS practice
- The only BIOS guidance: "POST/BIOS should use the DRAM configuration data stored in the CMOS RAM of the 82C206 IPC" [NEAT p49].
- Typical BIOS setup values are **NOT STATED**.

---

### A2. C&T CS8230 / CS8231 (386DX): 82C301 bus controller; 82C302 memory controller (CS8230) or 82C307 cache/DRAM controller (CS8231)

#### A2.1 Configuration access
- 22h is the index, **"W/O"** (write-only) in the port figure; 23h is data, "R/W DATA ACCESS" [CS8230 p15 Fig 1-4, p163 Fig 5.1].
- Every 23h access must be preceded by a 22h write [CS8230 p15–16, p163].
- Decode: the 82A304 generates `IO2XCS` for 022h/023h [CS8230 p83 Table 4-1, p88].
  - Its **EXDEC strap "when low ignores A<11:10>"** for system-board I/O [CS8230 p88]. **DERIVED:** 422h/822h/C22h then alias the index port.
- Index map:
  - **82C301**: 04h, 05h, 06h ("3 bytes") [CS8230 p16].
  - **82C302**: 08h–0Fh, 10h–13h, 28h–29h, plus 2Ah<0> [CS8230 p44–48]. **Rev C** adds 20h [CS8230 p49].
  - **82C307**: "24 bytes", including 08h/09h, 0Ah–0Fh, 2Ah, 10h–17h, 20h–29h, 30h… [CS8230 p163–174].
- **Invalid index on the memory controller:** "No data output and XDEN is inactive if the index set up by the previous IO22 Write doesn't point to a valid IO23 register of 82C302. Valid registers of IO23: 08H-0FH, 10H-13H, 28H-29H." [CS8230 p63; same note p210]. So an unclaimed index floats the bus (DERIVED: typically FFh).
- 82C301 behaviour on invalid indices: **UNKNOWN**.

#### A2.2 Identification
- **82C301 04h bits 7:6** = "Read only. Version, 0 Initial version". Bit 5 reserved. Bits 1 and 0 are read-only status (power-fail during last NMI; READY timeout) [CS8230 p16].
- **08h** [CS8230 p45, p49, p164]:
  - 82C302: bit 7 "Controller Type ... 0 Interleaved Memory Controller (82C302)", bits 6:5 Version 0 = initial. Rev C reads bits 6:5 = 01 [CS8230 p49].
  - 82C307: bit 7 = 1 "Cache controller (82C307)", bits 6:5 0 = initial.
  - Expected `08h & E0h`: **00h** (82C302 initial or rev B), **20h** (82C302 rev C), **80h** (82C307 initial).
- These agree with SURVEY's 86Box read masks (04h ~E3h, 08h ~E0h). 86Box's missing expected values are now known.
- Safest probe (DERIVED): `cli`; `out 22h,08h`; `in 23h`; `out 22h,04h`; `in 23h`; compare against a direct `in 21h`. Do **not** probe 10h–17h or 20h on a possibly-aliased board (A0: ICW1 / EOI).
  - On a NEAT board, index 08h is unclaimed by the 82C212 (valid 64h–6Fh) [NEAT p46], so it reads floating.
  - Reads have no documented side effect.

#### A2.3 Bus timing (82C301; common to CS8230 and CS8231) [CS8230 p16–17]

**04h**

| bit | meaning | default |
|---|---|---|
| 7:6 | Version (R) | 0 |
| 5 | reserved | — |
| 4 | Processor clock select: 0 = "Use processor oscillator input. Default." · 1 = "Use AT bus state machine clock (SYSCLKx2)" | 0 |
| 3 | Power-fail warning NMI enable | 0 |
| 2 | Local bus READY timeout NMI enable | 0 |
| 1 | (R) power-fail warning active during last NMI arbitration | 0 |
| 0 | (R) READY timeout occurred | 0 |

**05h, Command delay.** Each 2-bit field is 0–3 BCLK cycles.

| bits | field | default |
|---|---|---|
| 7:6 | AT bus 32-bit memory command delay | 00 |
| 5:4 | 16-bit memory command delay | 00 |
| 3:2 | 8-bit memory command delay | **01** |
| 1:0 | **I/O cycle** command delay | **01** |

- The book's encoding legend is misprinted as single bits, "0 0 cycle / 1 1 cycle / 0 2 cycle / 1 3 cycle" [CS8230 p17]. The intended reading (DERIVED from the field text "Specifies between 0 to 3 BCLK cycles") is binary n = n cycles.
- Default **05h = 05h** (DERIVED).
- p14 says commands are delayed "in one half units of ATSCLK (BCLK) within the selected wait states" [CS8230 p14].

**06h, Wait State / Bus Clock Source.** The encoding is **INVERTED** compared with NEAT.

| bits | field | encoding (verbatim) | default |
|---|---|---|---|
| 7:6 | 32-bit AT bus WS, "Each wait state is 2 BCLK or 1 ATSCLK" | 00 3 · 01 2 · 10 1 · 11 0 | "Default is 3" |
| 5:4 | 16-bit AT bus WS, "Each wait state is 2 BCLK cycles" | 00 3 · 01 2 · 10 1 · 11 0 | "Default is 3" |
| 3:2 | 8-bit AT bus WS, "selects 2-5 wait states" | 00 5 · 01 4 · 10 3 · 11 2 | "Default is 5" |
| 1:0 | Bus clock source | 00 "Use Proc Clock/3 for AT bus state machine. Default." · 01 "Proc Clock/2" · 10 "Reserved." · 11 "Use ATCLK input pin" | 00 |

- "Default is 5" can only be a wait-state count (5 → code 00), so by the same reading "Default is 3" is code 00, and the reset value is **06h = 00h**. **DERIVED, high confidence.** It also agrees with the clock-source default of 00.
- There is no separate I/O wait-state field: I/O takes the field for its transfer width (DERIVED from "per 8 bit transfer on the AT bus").

**Clocks** [CS8230 p9, p12, p14, p18]
- SYSCLK "is always BCLK/2" [p9]. The documented defaults are "CLK2 = CLK2IN, SYSCLK = CLK2IN/6" [p12].
- Table 1-1, "Examples of BCLK and SYSCLK" [p12]:

  | CLK2IN | SCLK | ratio | BCLK | SYSCLK |
  |---|---|---|---|---|
  | 24 | 12 | /2 | 12 | 6 |
  | 32 | 16 | /2 | 16 | 8 |
  | 32 | 16 | /3 | 10.7 | 5.4 |
  | 40 | 20 | /2 | 20 | 10 |
  | 50 | 25 | /3 | 16.7 | 8.4 |

- The ATSCLK pin "should have a nominal value in the range of 6 to 8 MHz for maintaining correct AT I/O bus timing with IBM PC AT" [p18]. **This is the book's only recommendation.**
- Book inconsistency: the asynchronous mode is given as "BCLK = ATCLK1, SYSCLK = BCLK/2 = ATCLK1/4" [p9]. That contradicts "SYSCLK is always BCLK/2" [p9] and "half the frequency of BCLK" [p18].
- Synchronous options listed [p9]: CLK2IN/2, CLK2IN/3, and CLK2 = BCLK = CLK2IN/2.

#### A2.4 Hazards
- **04h bit 4 vs 06h[1:0]:** "If SCLK is selected as the source for BCLK, CLK2 source must not be selected as BCLK" [p16]. Also "the processor clock source should be set to CLK2IN whenever the BCLK is selected to be SCLK" [p14]. Set 06h[1:0] = 01 only with 04h bit 4 = 0.
- **06h[1:0] = 11 (ATCLK)**: ATCLK1 "should be tied low if not used" [p18]. **DERIVED:** selecting it on such a board stops the AT bus. 10 is Reserved.
- 04h bits 3 and 2 arm NMIs. The 82C302/82C307 registers remap memory, shadow and A20-limit state (08h bits 4–0, 09h, 0Ah–0Fh, 2Ah<0> disables the low 256K) [p45–46, p164–167]: **do not write**.
- Persistence: "The RESET4 input causes all internal registers to be reset to their default values. Configuration registers not specified with a default value are not reinitialized" (82C302) [p44]. RESET4 is asserted "as long as the power good signal is held low" [p12]. A warm reset is RESET3 only [p12]. Whether the 82C301's own registers reset is **UNKNOWN**.

#### A2.5 BIOS practice
**NOT STATED.** The book contains no BIOS setup values.

---

### A3. C&T SCAT 82C235 (286) and SCATsx 82C836 (386SX)

- **Configuration access: UNKNOWN.** No register, port or index information exists in SF89 p11 or SF90 p34–35. The SURVEY's 86Box claims (22h/23h; readable to 4Fh/64h; 40h = 01h/04h/13h; 01h, 41h, 44h) are **unverified**.
- **Identification: UNKNOWN** from these books.
- **Bus timing:** the feature lists say only:
  - "80286 control logic and clocks which support CPU speeds of up to 12.5 MHz with 0 or 1 wait states"
  - "An 82284 compatible clock generation and READY interface"
  - "An 82288 compatible bus controller"
  - "Fast Gate A20 and Fast CPU Reset logic"
  - "Power management features"

  [SF89 p11; SF90 p34]. Register fields, encodings and defaults are **UNKNOWN**.
- **BIOS:** the C&T SCAT BIOS "is designed to allow the CHIPSet registers to be loaded from saved information during power-up. The registers and their values that will be loaded can be controlled by a program that is included in the software kit" [SF89 p23]. A "BIOS modification utility ... allows many common modifications of BIOS and CHIPSet configuration parameters, including ... the default CMOS values" [SF89 p22]. **DERIVED:** chipset values on SCAT boards are OEM-chosen and possibly CMOS-held.
- **SCATsx 82C836 is absent from both catalogs.** Everything about it is **UNKNOWN**.

### A4. C&T CS8220 (82C201/82C202/82A203–205), 286 at 6–10 MHz

- The catalog [SF90 p8] says:
  - "Variable Wait State Selection".
  - "Two signals, ALE and RAS can be altered with the activation of the Early Mode select line (EMODE). When EMODE is low, both signals become valid before the normal ALE and RAS signals."
  - "Variable wait state selection is also provided to accommodate slower memories and peripherals."
- EMODE is a **pin**, i.e. a board strap. No software register, port, ID or encoding is described: **UNKNOWN**. The SURVEY's direct registers at A4h/A5h/ABh are unverified here. A4h lies in the slave PIC's A0h–BFh range (SURVEY), so treat any probe there as **unsafe**.

### A5. C&T 82C206 IPC (in every chipset above)

- The catalog says [SF90 p30]:
  - "8 MHz DMA clock with programmable internal divider for 4 MHz operation".
  - "Programmable wait states for the DMA cycle".
  - "Programmable delays are provided for the CPU access to the internal registers".
  - "Reduced recovery time (120 ns) between control" for its 8237/8259/8254.
- Its block diagram shows a "CONFIG REGISTER" [SF90 p31].
- **Address, index and encoding: UNKNOWN.** The NEAT book defers to a separate "82C206 Integrated Peripherals Controller data book" [NEAT p2], which is not among these files.

---

### A6. C&T 82C100 (XT, 8088/8086/V20/V30; out of main scope)

#### A6.1 Access
- 22h is the index and 23h the data, "a two step process" with a mandatory 22h write before each 23h access [C100 p36].
- Indexed registers [C100 p37–41]:
  - 40h Clock/Mode Size, 41h System Configuration, 42h Configuration Valid, 43h DIP Switch Emulation
  - 44h–47h Substitute NMI vector, 48h Refresh Timer Counter, 49h Wait State Select/Refresh Enable/Keyboard Type
  - 4Ah Reserved, 4Bh Sleep/Memory Configuration, 4Ch EMS Configuration
- **Direct** ports: **72h** R/W NMI Control [C100 p41], **7Eh** R/W NMI Status, **7Fh** R/W Power Control and Reset [C100 p42], and **AxH** (x = 0–F) W NMI Control [C100 p43].

#### A6.2 Identification
**No ID or version register is documented. UNKNOWN.**
- 43h defaults to 30h ("1 floppy, monochrome display, 256KB, no 8087") [C100 p38].
- 42h bit 0 is "Valid Configuration", 0 at first power-up [C100 p38].
- Neither is a chip ID. On a 5150, indices 48h–4Bh written to 22h are OCW3s that reset special mask mode, and 4Ch–4Fh are poll commands (A0, DERIVED).

#### A6.3 Bus timing
- **40h** [C100 p37]:
  - bit 7 CLKSEL: 0 = 4.77 MHz (default), 1 = fast (max 10 MHz).
  - bit 6 duty cycle: 0 = 33% (default), 1 = 50% (V20/V30).
  - bit 2 "Two Clocks", which "must be set before the clock source can be switched to run at a higher speed".
  - bit 1 DMA clock: 0 = system clock, 1 = /2.
  - bit 0: 16-bit mode, **default 1**.
- **41h bit 3, CD** [C100 p37]: 0 = normal, commands asserted in T2. 1 = commands asserted in T3; the command is "shortened by 1 cycle. An additional wait state should be specified to maintain the normal command width".
- **41h bit 2, SM** [C100 p37]: 1 forces 0 WS for system memory (0–640K and EMS).
- **49h** [C100 p39]:
  - bits 7:6 MWS, memory: 00 = 0 (default) · 01 = 1 · 10 = 2 · 11 = 3.
  - bits 5:4 IOWS, I/O: **00 = 1 (default)** · 01 = 2 · 10 = 3 · 11 = 4.
  - bits 3:2 DMAWS: 00 = 1 (default) · 01 = 2 · 10 = 3 · 11 = 4.
  - bit 1 REFEN: 0 = refresh disabled (**default**), 1 = enabled.
  - bit 0 PS/2 keyboard.

#### A6.4 Hazards
- **40h bit 0 defaults to 16-bit mode**: "If the 82C100 is used with an 88/V20 then the BIOS must reset this bit immediately" [C100 p37].
- **49h bit 1 = 0 stops the DRAM refresh counter** [C100 p39]. **DERIVED:** loss of memory.
- **41h bit 6** enables software reset, and **7Fh bit 3 = 1 "Asserts System Reset"** (only with 41h bit 6 set) [C100 p37, p42].
- **7Fh bit 1** requests power off via a Suspend NMI [C100 p43].
- 4Bh bit 7 is sleep [C100 p39]. 41h bit 7 selects memory type, and 42h bits 3:2 RAM size [C100 p37–38].

#### A6.5 BIOS practice
- "The configuration setup should be the first task performed after power is turned on ... The configuration values can be stored in external battery-backed-up SRAM such as in the 82C606 CHIPSpak" [C100 p36].
- 42h bit 0 is "set to a 1 by the configuration routine after it has successfully set the configuration registers" [C100 p38].

---

### A7. Summary of corrections to SURVEY

1. **NEAT 62h 8-bit WS** is 00/01/10/11 = 2/3/4/5 WS, so the code for 5 WS is **3**, not 4. **16-bit WS** is 00–11 = 0–3 WS. The documented defaults are 5 WS and 3 WS, but p25 says 4 and 1 [NEAT p25, p31].
2. **NEAT 61h** is confirmed bit for bit. Defaults: I/O CD 1, 8-bit CD 1, 16-bit CD 0, quick mode disabled (1), hold delay off [NEAT p30].
3. **NEAT index space** is 60h–62h plus 64h–6Fh. 63h is unassigned [NEAT p29, p46].
4. **CS8230** now has documented ID values: 04h bits 7:6 = 0; 08h bit 7 = 0 (82C302) or 1 (82C307); bits 6:5 = 00 or 01 (82C302 rev C) [CS8230 p16, p45, p49, p164].
   - Its 05h/06h are full timing registers, *not* "unknown". The 06h wait-state encoding is **inverted** (00 = most waits), and its clock default is Proc/3 [CS8230 p17].
   - The 22h index port is write-only, and an invalid index floats the bus on the 82C302 [CS8230 p15, p63].
   - The SURVEY verdict "Not feasible without datasheet" is superseded.
5. **SCAT, SCATsx, CS8220 and the 82C206 register level are not in these books.** Every SURVEY claim about them stays unverified.

---

## VLSI Technology 286/386 AT chipsets: configuration, identification and ISA bus timing

Taken from the vendor data books, 2026-10-13. Every page number is the **PDF page** (as given to `pdftotext -f/-l`), not the printed folio. The tables marked "(image)" were read from a rendered page, because the OCR was garbled.

| key | file |
|---|---|
| **S1** | `datasheets/Data Sheets/VTI_VL82C310_82C311_82C311L_Data_Manual_199201.pdf`: SCAMP, 122 pp |
| **S2A** | `datasheets2/Data Sheets 2/VTI_VL82C315A_Data_Manual_199206.pdf`: SCAMP II, 158 pp |
| **S2B** | `datasheets2/Data Sheets 2/VTI_VL82C316_VL82C323_SCAMP_II_199210.pdf`: 210 pp |
| **TC** | `datasheets2/Data Sheets 2/VTI_TOPCAT_VL82C286_82C386_PC_AT_Chipset_Oct90.pdf`: VL82C320/330/331/332, 236 pp |
| **B90** | `datasheets2/Data Sheets 2/1990_VTI_Computer_Products.pdf`: VL82C100-104, 201-205A, 106; 508 pp |
| **B89** | `datasheets2/Data Sheets 2/1989_VTI_Computer_Products.pdf`: the same parts, an earlier edition |

---

### B0. Summary for the probe designer

**Three families have software registers, and all three use the same pair of ports: index ECh, data EDh.** The ID byte is always at index 00h. Code the chip by the 2-bit groups of that byte (each book explains its code this way):

| Family | Part | Reg 00h value per book | Stable mask -> value | Cite |
|---|---|---|---|---|
| SCAMP | VL82C310 | **D1h** (Table 24); text: first version **D2h** | `& FCh` = D0h | S1 p75, p77 (image) |
| SCAMP | VL82C311 / 311L | **D5h** (Table 24 note); text: **D6h** | `& FCh` = D4h | S1 p75, p77 (image) |
| SCAMP II | VL82C315A | **6Ah** (D7:6 = 01, D5:3 = 101, D2:0 = version) | `& F8h` = 68h | S2A p112, p113 (image) |
| SCAMP II | VL82C316 | **6Ah**. The text is the same word for word and claims to decode as "316" | `& F8h` = 68h | S2B p117, p118 (image) |
| TOPCAT | VL82C320 (286/386SX system controller) | **E0h**: "320" Rev 0 | `& FCh` = E0h | TC p63, p66, p67 |
| TOPCAT | VL82C330 (386DX system controller) | **F2h**: "330" Rev 2 | `& FCh` = F0h | TC p114 |
| TOPCAT | VL82C331 (ISA bus controller, both sets) | **F4h**, at **index 80h** | `& FCh` = F4h | TC p158, p160 (image) |

**The VL82C100/101B/102A/103A/104 and VL82C201/202/203/204/205A 286 sets have no software-programmable bus timing at all.** Wait states, command delays and SYSCLK are fixed by internal logic and set by strap pins. The evidence is in section 4. Those boards cannot be identified through any VLSI-defined ID register, because none exists.

**A probe that the books support.** It is read-only except for index writes. Run it with interrupts off.
1. `in al,ECh`. Save the value; it is the BIOS's current index. The book says the index is readable and "always returns the last value written" (S1 p63, S2A p103 Table 53, TC p63).
2. `out ECh,00h`, then `in al,ECh`. The result must be 00h. This guards against a locked chip: on SCAMP a locked chip ignores the index write (section 1.4), and on TOPCAT it blocks all access.
3. `in al,EDh`. Classify the byte by the table above.
4. Only if the byte matched E0h or F0h (TOPCAT): `out ECh,80h` and `in al,EDh`. The result must match `& FCh = F4h`, the VL82C331.
5. Optional second witness, read-only: bits the books say read back as 1.
   - SCAMP: 06h bits 7:2, 05h bits 7:5, 0Ah bits 7:4, 14h bits 4:3, 16h bit 4 (S1 p77).
   - TOPCAT 320: index 01h (COMPAT) reads FFh (TC p63).
   - TOPCAT 331: 84h bits 7, 5 and 4 (TC p160).
   - SCAMP II reserved bits are "undefined", so they are useless as a witness (S2A p113 note).
6. Write the saved index back to ECh.

**Never touch these ports, in any step:**
- EEh: a read enables A20 and a write disables it.
- EFh: **a read resets the CPU**.
- E8h-EBh: EMS. A read of EBh auto-increments the index.
- F0h, F1h, F4h, F5h: coprocessor clear and reset, and CPU slow and fast.
- F9h and FBh: config lock and unlock.
- 92h.
- On SCAMP II with the VL82C323 PMU: indices **C0h-DFh**. Reading the PMU's Supply register is the unlock action.

**Two places where the emulator disagrees with the book.** 86Box `scamp.c` is the only emulated part. It returns **FFh for a read of ECh** (`scamp_read` has no `case 0xec`), and it stores reserved bits as 0. Steps 2 and 5 would therefore reject the emulated SCAMP that the book says they accept. The survey's "readable 00h-16h" holds only for EDh. The survey's "02h-16h mostly memory map; no timing decode" is **wrong** by the book: 07h, 15h and 16h are clock, ROM/DMA and bus timing (section 1.3).

---

### B1. SCAMP: VL82C310 / VL82C311 / VL82C311L (286 or 386SX; strap selects the CPU)

#### B1.1 Configuration access
- **Ports.** ECh is the index register, R/W. EDh is the data port, R/W. "All subsequent Data Port reads and writes access the register at this address until the Index Register is written with a new address. This register is readable. It always returns the last value written to it." (S1 p63; repeated p75: "The Index Register in the SCAMP Controller is read/write").
- **Other dedicated ports** (Table 18, S1 p62):

  | Port | Function |
  |---|---|
  | E8h | EMS index |
  | E9h | reserved |
  | EAh, EBh | EMS data |
  | EEh | Fast A20 |
  | EFh | Fast Reset |
  | F0h | coprocessor busy clear |
  | F1h | coprocessor reset |
  | F4h | Slow CPU |
  | F5h | Fast CPU |
  | F9h | Configuration Disable |
  | FBh | Configuration Enable |

  F8h, FAh, FCh and FEh are the coprocessor. "These decodes can be disabled in case of a conflict" applies to EEh, EFh, F4h, F5h, F9h and FBh.
- **Implemented indices.** 00h (VER, R-O), 02h SLTPTR, 03h RAMMAP, 05h RAMSET, 06h REFCTL, 07h CLKCTL, 0Ah MCDCTL (310 only), 0Bh EMSEN1, 0Ch EMSEN2, 0Eh ABAXS, 0Fh CAXS, 10h DAXS, 11h FEAXS, 13h SLPCTL, 14h MISCSET, 15h ROMDMA, 16h BUSCTL (Table 23, S1 p76, image).
- **Reads of unimplemented indices** (01h, 04h, 08h, 09h, 0Dh, 12h, 17h and up): **UNKNOWN.** The book does not say. 86Box returns its zeroed array for 01h-16h and FFh above 16h.
- **Reserved bits** "read-only and always return logic 1" (S1 p28, p29, p71-74 legend, Table 24 p77).
- **Lock.**
  - A dummy write to FBh enables the configuration registers and a dummy write to F9h disables them. "When disabled, the system is locked out from any write access to the configuration and control ports from address **E8h through EFh**" (S1 p65, image check). That range includes the index port ECh. A read of F9h or FBh returns undefined data.
  - If MISCSET.-VSF = 1, the enable/disable feature is itself disabled (S1 p65).
  - **Reset state of the lock: UNKNOWN** (not stated). 86Box powers up enabled.

#### B1.2 Identification
- VER (00h, R-O): "D2-D7 contain a read-only code which indicates that this part is a VLSI Technology, Inc. SCAMP Controller. D0 and D1 contain the version number... The first version of the SCAMP Controller contains the code D2h... '310' Rev '2'. The code in the VL82C311 and VL82C311L is D6h." (S1 p75).
- **Internal conflict.** Table 24 gives the VER reset value as `1101 0001` = D1h, with the note "The reset value of VER is binary 11010101 [D5h] in the VL82C311 and VL82C311L" (S1 p77, image). The summary page also draws `1101 0001` (S1 p71, image). Both readings agree on bits 7:2. **Match on `& FCh` (D0h = 310, D4h = 311/311L) and treat bits 1:0 as revision.** 86Box hard-codes D6h (`scamp.c:46`).
- **On a board without this chip.** No VLSI book describes ECh/EDh on other hardware. In the local 86Box tree, ECh-EFh is also decoded only by VLSI's VL82C480 (a 486 part; its index 00h reads FFh there), and Dell's jumper device sits at E8h/E9h. A floating bus gives FFh at EDh, which matches no code.
- **Side-effecting reads and writes nearby.**
  - Reading **EFh** resets the CPU: "A dummy read of EFh resets the processor and returns a value of FFh". The reset follows a 6.72 us delay and lasts 16 CLK2 (S1 p64, also p20).
  - Reading **EEh** enables A20 and a write disables it (S1 p63).
  - Both are gated by MISCSET.-VSF, and the POR value 0 means enabled (S1 p61).
  - Port 92h bit 0 = 1 also resets the CPU (S1 p64).
  - A write to **F1h** resets the coprocessor (S1 p64).
  - A write to **F4h** slows the CPU (S1 p65).

#### B1.3 Bus timing
CLOCK in this register means BUSOSC if an oscillator is fitted on that pin, and TCLK2 (the 2x CPU crystal) otherwise. SYSCLK *is* the AT bus clock. It is 1/2, 1/4, 1/6 or 1/8 of TCLK2 or BUSOSC (S1 p16 SYSCLK pin; p49).

**CLKCTL, index 07h** (S1 p50, p72, p76, p77):
- **bit 7 ENVDSP.** 1 = switch SYSCLK to the fast divider during A0000h-BFFFFh accesses. POR 0.
- **bits 6:5 CLK2DIV.** The CPU clock in non-turbo mode (TURBO pin low or a write to F4h): 00 = TCLK2÷1, 01 = ÷2, 10 = ÷3, 11 = ÷4. POR 00. *Not bus timing, but writing it changes CPU speed.*
- **bits 4:3 FCLKDIV, the fast SYSCLK divider.**
  - Per S1 p50: 00 = CLOCK/2, 01 = CLOCK/4, 10 = CLOCK/6, 11 = **Reserved**. Default 10.
  - **Conflicts inside the book:**
    - The summary (S1 p72) says 11 = "No Change".
    - Table 16 (S1 p49) leaves the 11 row blank.
    - The block diagram (S1 p48) labels the fast divider "÷1, ÷2, ÷3".
    - SCAMP II defines the same bits as ÷1/÷2/÷3 (section 2.3).
  - **Treat the encoding as UNCERTAIN for SCAMP I, and never write 11.**
- **bit 2 BOSCSNS.**
  - Without a BUSOSC oscillator, it is read-only and shows the BUSOSC pin.
  - With one, it is R/W, and 1 selects the fast divider.
  - POR is 0 if an oscillator is present, otherwise the pin state (S1 p50, p77 note 1).
- **bits 1:0 SCLKDIV, the slow (normal) SYSCLK divider.** 00 = ÷2, 01 = ÷4, 10 = ÷6, 11 = ÷8. POR is latched from the -DKEN and -PPICS strap pins; the default in Table 22 is 01 (S1 p49, p70).
- How the divider is chosen (Table 16, S1 p49):
  - The slow divider is used when BOSCSNS/video = 0.
  - The fast divider is used when BOSCSNS = 1, or when a video DRAM access happens with ENVDSP = 1.

**ROMDMA, index 15h** (S1 p47, p74, p77). POR = `PAR1 PAR0 1 1 1 0 0 0`.

| Bits | Field | Encoding | POR |
|---|---|---|---|
| 7:6 | ROMWS1:0, BIOS ROM wait states "timed in slot bus cycles" | 00 = 3, 01 = 1, 10 = 2, 11 = 3 | strap pins PAR1/PAR0 |
| 5:4 | DMAWS8, command-active clocks for 8-bit DMA | 00 = 2, 01 = 4, 10 = 3, 11 = 3 DMA clocks | 11 |
| 3:2 | DMAWS16, the same for 16-bit DMA | 00 = 2, 01 = 4, 10 = 3, 11 = 3 | 10 |
| 1 | DMACLK | 0 = SYSCLK/2, 1 = SYSCLK | 0 |
| 0 | MEMTM | 1 = -MEMR falls one DMACLK earlier on memory-to-I/O DMA | 0 |

**BUSCTL, index 16h** (S1 p54 image, p74, p76, p77):

| Bit | Name | Meaning | POR |
|---|---|---|---|
| 7 | ROMWID | 1 = 16-bit BIOS ROM | strap MA10 |
| 6 | SLTDRV | slot drive: 0 = 12 mA, 1 = 24 mA | pin -ROMCS |
| 5 | DSKTMG | "Disk Timing... If set (1), normal programmed I/O... If cleared (0), slow programmed I/O" | **0 per S1 p54, but 1 per Table 24 (S1 p77). Conflict.** |
| 4 | reserved | reads 1. The POR row on S1 p54 prints 0 here, against the bit legend and Table 24 | |
| 3 | CMDLY2 | +1 command delay "for 8-bit and 16-bit I/O cycles and for 8-bit memory cycles"; 0 = none (PC/AT) | 0 |
| 2 | CMDLY1 | +1 command delay on 16-bit memory cycles; 0 = PC/AT zero delay | 0 |
| 1 | 16WS | 0 = "PC/AT-compatible zero wait states", 1 = one wait state "for 16-bit slot bus accesses" | 0 |
| 0 | 8WS | 0 = four wait states on 8-bit slot accesses, 1 = five | 0 |

Notes on BUSCTL:
- Fewer than 4/0 wait states come only from a card asserting -WS0. More come from IOCHRDY (S1 p54).
- **The baseline the 16WS bit modifies is ambiguous.** The -IOCHRDY pin text gives CPU defaults of "four wait states for 8-bit peripherals, one wait state for 16-bit peripherals, and three wait states for ROM" (S1 p15). The -IOCS16 and -MEMCS16 pin text says a 16-bit I/O access takes "one wait state with one command delay" and a 16-bit memory access "one wait state with no command delays" (S1 p15). TOPCAT's twin bit is explicit about it (section 3.3).
- **Units:** "a command delay of one BUSOSC cycle" (S1 p15). The unit of a slot wait state is not stated: **UNKNOWN**, probably SYSCLK.
- **Recommended values per CPU clock: none given anywhere in S1.**

#### B1.4 Hazards
- EFh, EEh, F1h and F4h are as listed in 1.2. A hot reset needs A20 enabled first (S1 p64).
- **The lock covers ECh.** If the BIOS left the chip locked (F9h), an index write is silently dropped. Data reads then come from the *previous* index, which is why the probe reads ECh back.
- Writing CLKCTL bits 6:5 changes the non-turbo CPU clock. Writing bits 1:0, 4:3 or 2 changes SYSCLK for every card at once.
- SLPCTL (13h) bit 2 "should never be written, it is reserved for a special test mode" (S1 p74).
- No register is documented as write-to-reset. In MISCSET (14h), clearing bit 7 (-VSF) is what *enables* the EFh/EEh traps; it is 0 (enabled) at POR (S1 p61).

#### B1.5 BIOS
- The book says only that the straps let "a generic BIOS" run (S1 p70), and that "a smart BIOS can compensate for 'feature' differences based on the version number" (S1 p75).
- The Phoenix "PhoenixBIOS A386SX" is listed as supporting it (S1 p114).
- No programming sequence for BUSCTL or CLKCTL is given.

---

### B2. SCAMP II: VL82C315A, and VL82C316 with the VL82C323 PMU (386SX)

#### B2.1 Configuration access
- **Ports** are the same as SCAMP I: ECh index (R/W, "always returns the last value written"), EDh data (S2A p103 Tables 52-54; S2B p108).
- The EMS/memory-card index is at E8h. Its data ports are EAh and EBh.
- **Implemented indices** (Table 64; S2A p113, S2B p118, both images):
  - 00h, 02h, 03h, 05h, 06h, 07h, 08h, 09h, 0Ah
  - 0Eh-11h, 13h-1Dh
  - 80h-87h (SMM, R/W-special in SMM)
  - 8Ah-8Eh (I/O trap timers)
- **Reserved bits** "are currently undefined... registers should always be written such that the value of these bits is not altered" (S2A p113 note). Read-modify-write is mandatory.
- **Unimplemented indices:** UNKNOWN.
- **Lock:** F9h disables and FBh enables, as on SCAMP I. "Locked out from write access to the configuration and control ports from address **ECh through EFh**" (S2A p106; S2B p111). The reset state is UNKNOWN.
- **VL82C323 PMU** (S2B only):
  - It sits on the same ECh/EDh pair: "The VL82C323's register range is from C0h to DFh."
  - "The Index Register is a write-only register in the VL82C323". Reads of ECh come from the SCAMP.
  - Its data-register writes are locked at power-up. "To unlock it for a write access, the CPU must first read the Supply Register" (S2B p170).

#### B2.2 Identification
- VER (00h): "D7 and D6 contains the code 01. D5-D3 contains the code 101. D2-D0 contains the version number of this chip. D2-D0 will be the only bits in this register that will ever change value... The first version of this chip contains the code 6Ah" (S2A p112).
- **S2B p117 repeats the text word for word for the VL82C316**, with the same 6Ah. **So by the books, 315A and 316 cannot be told apart.** Match on `& F8h` = 68h.
- Side-effect ports are the same as SCAMP I:
  - EFh dummy read resets the CPU, gated by -VSF (S2A p104).
  - Port 92h bit 0 resets (S2A p110).
  - EEh is A20 (S2A p104).
- Avoid C0h-DFh on 316 systems because of the PMU unlock.

#### B2.3 Bus timing
**CLKCTL, index 07h** (S2A p61; S2B p65): the same layout as SCAMP I, except FCLKDIV.
- **bits 4:3 FCLKDIV**: **00 = CLOCK÷1, 01 = ÷2, 10 = ÷3, 11 = Reserved**. Default 10.
- bits 1:0 SCLKDIV: 00 = ÷2, 01 = ÷4, 10 = ÷6, 11 = ÷8. POR comes from the DKEN and -RAMW/-WE0 pins (S2A p60-61, p111 Table 63, default 01).
- SYSCLK ranges from 5 to 16 MHz (S2A p60).
- SCAMP II adds a second fast-divider trigger: PCMCIA memory accesses with -ENMCSPD (MCDCTL 0Ah bit 7; S2A p60, p62).

**ROMDMA, index 15h** (S2A p58): the same encodings as SCAMP I. **POR = `PAR1 PAR0 1 0 1 0 0 0`**, so DMAWS8 = 10 and DMAWS16 = 10, 3 clocks each (also Table 65, S2A p114).

**BUSCTL1, index 16h** (S2A p65; S2B p69). POR `MA10 x 1 0 x 0 x 0`. **Note:** the CMDLY bits are in different positions from SCAMP I.

| Bit | Name | Meaning | POR |
|---|---|---|---|
| 7 | ROMWID | 1 = 16-bit BIOS ROM. "ROMWID is a read-only bit. This is set by jumper on power-up" | strap MA10 |
| 6 | R | reserved | |
| 5 | DSKTMG | **new meaning**: "When set to 1, the first memory cycle after an I/O cycle is run at the programmed Non-Turbo frequency... When set to 0... at full speed" | 1 |
| 4 | LBAEN | local-bus -LBA enable | 0 |
| 3 | R | reserved | |
| 2 | **CMDLY2** | +1 command delay, 8/16-bit I/O and 8-bit memory | 0 |
| 1 | R | reserved | |
| 0 | **CMDLY1** | +1 command delay, 16-bit memory | 0 |

**BUSCTL2, index 17h** (S2A p66; S2B p70). POR `1 1 x x x 0 x 0`.

| Bit | Name | Meaning | POR |
|---|---|---|---|
| 7 | -CLK2OFF | sleep-mode clock gating | 1 |
| 6 | -DMAPWR | DMA clock power management | **S2A text says default 0; the S2A POR row and S2B text say 1. Conflict.** |
| 5:3 | R | reserved | |
| 2 | **16WS** | "0 = 0WS, 1 = 1WS", 16-bit slot | 0 |
| 1 | R | reserved | |
| 0 | **8WS** | "0 = 4WS, 1 = 5 WS", 8-bit slot | 0 |

- IOTMR and the other registers at 8Ah-8Eh are SMM I/O trap timers, **not I/O recovery** (S2A p113).
- No recommended values per CPU clock are given.

#### B2.4 Hazards
The same as SCAMP I, plus the PMU's indices C0h-DFh. A SCAMP I driver must **not** be reused here: 16h has changed layout, and the wait-state bits moved to 17h.

#### B2.5 BIOS
- SLPCTL bits "should be set with the desired values by the BIOS during POST" (S2A p62).
- The BIOS must enable A20GATE before a software reset (S2A p64).
- Straps let "a generic BIOS" run, and ROMWID and ROMLOC must still be strapped (S2A p111).
- No timing setup sequence is given.

---

### B3. TOPCAT: VL82C286-set (VL82C320 + VL82C331 + VL82C332, for 286/386SX) and VL82C386-set (VL82C330 + VL82C331 + VL82C332, for 386DX)

#### B3.1 Configuration access
- The **system controller** (320 or 330) owns ECh and EDh.
  - ECh: "This register is readable. It always returns the last value written to it."
  - "The Configuration Registers can only be accessed via the CPU and are limited to byte reads and writes" (TC p63).
- The **bus controller (331) shares the pair.**
  - "A write to the index register at 00ECh is latched into both the System Controller and Bus Controller. Only the System Controller index register is readable in order to avoid bus collisions."
  - "All data ports in the Bus Controller are located in the range 80h to FFh to avoid contention with data ports located in the System Controller. However, there is one exception": SLEEP at 13h is shadowed write-only, and readable at 83h for test (TC p158).
  - The 331 decodes 00E0h-00EFh (TC p140).
- **Indices.**
  - 320: 00h-16h (Table 14, TC p66). Unused bits "read back as logic 1".
  - 331: 80h-85h, plus write-only shadows at 03h, 06h, 13h and 14h (Table 13, TC p160, image).
  - The 331's shadow registers are "write-only registers. The bus will not be driven during a read", so the system controller supplies the read (TC p159-160).
- **Lock** (F9h disables, FBh enables): "When disabled, the system is locked out from **any access** to the configuration and control ports from address E8h through EFh" (TC p57).
  - This blocks *reads* too, which is stricter than SCAMP.
  - The 331 tracks the lock by watching F9h, FBh and MISCSET bit 7 (TC p158-159).
  - The reset state is UNKNOWN.

#### B3.2 Identification
- **VL82C320, VER at 00h:**
  - "D2-D7 contains a read-only code... (VL82C320). D0 and D1 contain the version number... The first version of this chip uses the code E0h... '320' Rev '0'" (TC p63).
  - The map row reads `1 1 1 0 0 0 0 0` (TC p66), and the reset table marks VER bits 7:5 as "1R" (TC p67).
- **VL82C330, VER at 00h:** "The first production version of this chip uses the code F2h... '330' Rev '2'" (TC p114).
- **VL82C331, VER at index 80h:** default F4h. "D2-D7 will contain a code... D0 and D1 contain the version number... The code 00F4h is used in the initial version... '331' Rev '0'" (TC p158). Table 14 reads `1R 1R 1R 1R 0R 1R 0R 0R` (TC p160, image).
- **Second witness:** COMPAT at 01h. "Bits 7-0 are read-only and always return logic 1's" (TC p63).
- **Side effects:**
  - EFh: "A read of EFh resets the processor", disabled if MISCSET bit 7 = 1 (TC p56).
  - EEh read: Fast A20 on (TC p55).
  - F1h write: NPX reset (TC p56).
  - E9h: "A read of this register activates the standard EMS register set. A write activates the alternate" (TC p55).
  - TEST (15h) "must never be written during normal operation" (TC p65).

#### B3.3 Bus timing
**The bus clock comes from the system controller's MISCSET, index 14h** (TC p36, p65). The 330 is the same (TC p89, p116).
- "BUSCLK... runs at twice the final AT slot bus frequency."
- Bits 2:1 are "the value used for division of TCLK2 or BUSOSC to generate BUSCLK": 00 = ÷1, 01 = ÷2, 10 = ÷3, **11 = ÷4 (Default)**.
- Bit 0 is the BUSCLK divider source: 0 = TCLK2 (default, synchronous), 1 = BUSOSC.
- "When a 16 MHz external oscillator is used, a ÷1 results in 8 MHz bus operation. Power-on reset defaults to ÷4, synchronous mode."
- 320 only: bits 4:3 are the slow (non-turbo) CLK2 divider, 00 = ÷1 (default), 01 = ÷2, 10 = ÷3, 11 = ÷4. On the 330, bit 5 is MBIOS.
- Bit 7 is the FX enable (0 = the EEh/EFh/F4h/F5h/F9h/FBh features enabled). Bit 6 is F1h control.
- Default 06h.

**CTRL1, index 16h, bit 7 DSKTMG** (320, TC p65): it forces "the first memory cycle after an I/O cycle to be executed at non-turbo speed". 0 = slow programmed I/O (default), 1 = normal.

**ROMDMA, index 81h, in the VL82C331** (TC p158, p160). Default FCh.

| Bits | Field | Encoding | Default |
|---|---|---|---|
| 7:6 | ROM wait states "timed in slot bus cycles" | 00 = 3, 01 = 1, 10 = 2, 11 = 3 | 11 |
| 5:4 | 8-bit DMA command clocks | 00 = 2, 01 = 4, 10 = 3, 11 = 3 | 11 |
| 3:2 | 16-bit DMA | the same | 11 |
| 1 | DMA clock | 1 = SYSCLK, 0 = SYSCLK/2 | 0 |
| 0 | -DMAMEMR | 1 = one DMACLK earlier | 0 |

The prose on TC p147 says the DMA default is "one wait state" with 0, 1 or 2 programmable; that does not match this clock-count encoding. **Conflict.**

**BUSCTL, index 84h, in the VL82C331, R/W** (TC p156, p159, p160). Default B0h.

| Bit | Name | Meaning | Default |
|---|---|---|---|
| 7 | reserved | reads 1 | 1 |
| 6 | RTC Ctrl | **1 disables the internal RTC** and redefines the PS/-RCLR/-IRQ8 pin | 0 |
| 5:4 | reserved | read 1 | 1 1 |
| 3 | ADDLY | +1 command delay for 8/16-bit I/O and 8-bit memory | 0 |
| 2 | RLX Timing | +1 command delay on 16-bit memory | 0 |
| 1 | 16 WS | 1 = +1 wait state for 16-bit slot: "two instead of the normal one for I/O accesses and one instead of zero for memory accesses" | 0 |
| 0 | 8 WS | 1 = five instead of four on 8-bit slot accesses | 0 |

- "On reset, the four control bits are reset to 0. This specifies the full PC/AT-compatibility mode" (TC p156).
- The 331 pin text gives the CPU defaults: 4 WS 8-bit, 1 WS 16-bit, 3 WS ROM, and one DMA wait state (TC p137).
- Units: UNKNOWN beyond "slot bus cycles".
- No recommendations per CPU clock.

#### B3.4 Hazards
- EFh, EEh, E9h, F1h and the TEST register, as above.
- **BUSCTL bit 6 kills the on-chip RTC.** Read-modify-write is required and the bit must be preserved.
- 331 MISCSET, RAMMAP and REFCTL at 14h, 03h and 06h are write-only shadows. A write to 06h bit 3 switches **both** chips between 16-bit and 10-bit I/O decode (TC p159).
- The full lock (any access) means a locked chip fails the ID read. That is the correct, safe outcome.

#### B3.5 BIOS
- The AMI BIOS listed for the set offers "Easy Extended CMOS Setup... Select Main and Bus Clock sources, Select Main Memory Waitstates".
- It also offers "Advanced Extended CMOS Setup... program the individual chipset registers directly" (TC p221).
- Phoenix 80386 BIOS support is on TC p226.
- No register sequence is given.

---

### B4. The VLSI 286 sets: VL82C100 / 101B / 102A / 103A / 104 and VL82C201 / 202 / 203 / 204 / 205A

#### B4.1 Configuration access
- **None for timing.** The system controllers (101B, 201) decode only these I/O ports:
  - the coprocessor's F0h and F1h: a write to F1h resets the 80287, and a write to F0h or F1h clears -BUSY286;
  - -NPCS for F8h-FFh;
  - -PPICS for the keyboard controller.

  Cites: B90 p62 (101B), p125 (201).
- The only programmable registers in the sets are:
  - **VL82C202 shadow-RAM RER/WPR at 09Fh.** "Eight consecutive I/O writes to 09Fh are required to unlock the configuration registers. The ninth I/O write to 09Fh accesses the RER and the tenth the WPR... cleared by an -IOW to any address except 09Fh, by any -IOR or by RESET... A read of this address will access first the RER, then the WPR." Bits 6 and 7 read 1 (B90 p147-148, Tables 1-2 p148-149). This is memory mapping, **not bus timing**.
  - Port A at **92h** in the 202: bit 0 drives SWRST, a CPU reset. Bit 1 is the alternate A20 (B90 p147, p145).
  - VL82C100: the standard AT DMA, PIC, PIT and page ports (B90 p32-53).

#### B4.2 Identification
**No ID or revision register exists in any of these parts.** That is shown by the absence of any such register in each data sheet's register and port description (B90 p54-77, p116-201). A VL82C202 board could at best be fingerprinted by RER/WPR at 9Fh. That is not recommended: a read of 9Fh advances the RER/WPR pointer and clears the unlock counter, and a write counts toward the unlock.

#### B4.3 Bus timing: strapped or fixed, never programmable
- **VL82C101B (B90 p57, p61-62).**
  - "Wait states can be controlled from a number of different sources within the VL82C101B. It is internally programmed to generate the wait states shown in Table 1 based on the appropriate input signals."
  - Table 1:

    | Cycle | Wait states |
    |---|---|
    | INTA | 4 |
    | 8-bit I/O | 4 |
    | 16-bit I/O | 1 |
    | off-board 8-bit memory | 4 |
    | off-board 16-bit memory | 1 |
    | ROM | 1 or 2, by **ROMWTST pin** |
    | RAM write | 1 |
    | RAM read | 0 or 1, by **RAMWTST pin** |

  - "Internal circuitry is used to insert one PROCCLK cycle of command delay for all I/O cycles and off-board 8 bit memory cycles."
  - SYSCLK "is equal to half the PROCCLK frequency" (B90 p58). The bus is therefore synchronous at CPU/2 with no divider.
  - Only IOCHRDY or -WS0 can change a cycle.
- **VL82C201 (B90 p118-125).**
  - Table 1 (p125) is the same, with ROM at 2 or 3 wait states (ROMWTST) and RAM read/write at 0 or 1 (**RAMRDWT** and **RAMWRWT** pins). A "Command Delay" column reads Yes for INTA, 8-bit I/O, 16-bit I/O and off-board 8-bit memory, and No for the rest.
  - The clock modulator is set by the **ENMODL pin**: "When ENMODL is low, clock modulation is disabled and PROCCLK is forced to 1/4 the frequency of FCLK" (p118). When modulation is enabled, PROCCLK is "FCLK/2 except for I/O cycles, off-board memory cycles, and DMA cycles. The frequency of PROCCLK will switch to FCLK/4 during those cycles" (p120).
  - SYSCLK = PROCCLKIN/2 (p120).
  - All of these are pins.
- **VL82C205A, the page-mode controller:** "Inputs -PAGE, RAMWRWT and RAMRDWT should be strapped to VDD or VSS" (B90 p192). Its configuration pins "are jumpers" (p187).
- **VL82C100:** "the VL82C100 ready control logic forces one wait state on every DMA transfer". The -XMEMR delay of one DMA clock is fixed (B90 p39-40).
- **The 1989 edition** (B89 p53-198) has the same pin-strap scheme: VL82C101 FASTMODE, ROMWTST and RAMWTST (p56-61), and VL82C201 ENMODL (p117-118). It contains no register.

**Conclusion: on these boards, AT bus timing cannot be read or changed by software.** A package should report "fixed by jumpers/straps". The 286 IDs in the survey come from the BIOS string (`286` = "standard 286, VLSI, G2 286") and agree with that.

#### B4.4 VL82C106 Combo I/O (often paired with these sets)
- It has **programmable per-chip-select I/O wait states, stored in the RTC CMOS at 69h-7Fh** and reached through ports 70h/71h (B90 p461-462).
- Each chip select (CS1-CS7) has three bytes: base LSB, base MSB and Range. The Range byte holds:
  - bits 4:0: don't-care address bits;
  - **bits 6:5 WS1:WS0: 00 = 0, 01 = 1, 10 = 3, 11 = 7 wait states**, where "Number of wait states = number of SYSCLK cycles IOCHRDY is forced inactive" and "can only extend the I/O cycle";
  - bit 7: 8/16-bit (1 = 8-bit, no -IOCS16).
- These take effect only when Control Register 1 (CMOS 6Ah) bit 3 CS_MODE = 1. At reset, the hard-wired decodes are used.
- **Hazard:** CMOS 69h and 6Ah are not battery-backed (B90 p461 note). The rest are general-purpose-looking CMOS bytes that can **move COM, LPT, FDC and IDE**. A VL82C106 has no ID either.

---

### B5. Where the book disagrees with the survey or with 86Box

| Topic | Book | Survey / 86Box |
|---|---|---|
| SCAMP ID | 310: D1h/D2h. 311/311L: D5h/D6h. Bits 1:0 are the revision (S1 p75, p77) | D6h only (`scamp.c:46`). Match on `& FCh` |
| ECh readable | Yes, it returns the last value written (S1 p63, p75) | 86Box returns **FFh** for ECh |
| Reserved bits | Read 1 on SCAMP I and TOPCAT | 86Box stores them as written or 0 |
| Lock (F9h) | SCAMP: blocks writes only, E8h-EFh. TOPCAT: blocks any access | 86Box SCAMP: blocks reads and writes (`cfg_enable`) |
| Timing registers | SCAMP 07h, 15h, 16h; SCAMP II 07h, 15h, 16h, 17h; TOPCAT 320/330 14h, 16h and 331 81h, 84h | Survey: "no timing decode (c)". 86Box stores the bits and models no ISA speed |
| VL82C100-series registers | **None** | Survey: "registers UNVERIFIED". Settled: none |
| SCAMP II ID | 6Ah for both 315A and 316 | Not modelled |
| SCAMP FCLKDIV | /2, /4, /6, reserved (S1 p50), against /1, /2, /3 (S1 p48 figure; S2A/S2B) | UNCERTAIN; never write 11 |
| SCAMP DSKTMG POR | 0 (S1 p54) against 1 (S1 p77) | Read it; do not assume |

**Not determined from these books (UNKNOWN):**
- the value read from an unimplemented index;
- the reset state of the F9h/FBh lock;
- the clock unit of a slot wait state on SCAMP and TOPCAT (both say "slot bus cycles");
- I/O recovery as a separate field. No VLSI part here has one beyond the command-delay bits;
- any recommended values per CPU clock. None is given.

---

## Bus-timing registers from the data books: OPTi, UMC, WD FE3600 / WD76C10, ALi M6117D

**Source rule.** Every fact below cites a file and a **PDF page** (the page number pdfinfo or pdftotext counts, which is not the printed folio). File abbreviations:

| tag | file |
|---|---|
| **281** | `Data Sheets/82C281_OPTi_Cache_Sx_AT_199108.pdf` |
| **283** | `Data Sheets/82C283_386SX_System_Controller_199409.pdf` |
| **291** | `Data Sheets/82C291_SXWB_PC_AT_Chipset_199410.pdf` |
| **391** | `Data Sheets/82C391_82C392_OPTi-386WB_PC_AT_Chipset_199012.pdf` |
| **U231** | `Data Sheets/UM82C231.pdf` |
| **U212** | `Data Sheets/UM82C212.pdf` |
| **WD91** | `Data Sheets/16_WD75C10_76C10.pdf` (1991 book, sheet dated 10/3/90) |
| **WD93** | `Data Sheets/05-WD76C10A.pdf` (1993 book, sheet dated 3/19/92) |
| **TOC** | `Data Sheets/00_TOC.pdf` |
| **M61** | `Data Sheets/M6117D_Datasheet.pdf` |
| **FE01** | `uploads/e91a93aa-01_FE3001.pdf` (Advance Information 11/20/90) |
| **FE01A** | `uploads/1269c279-02_FE3001A.pdf` (Advance Information 10/24/90) |
| **FE36** | `uploads/395e3f88-FE3600.pdf` (FE3600B overview, ©1988) |
| **NEAT** | `Data Sheets/CS8221_NEAT_Data_Book_198710.pdf`, used only as a cross-reference |

Tags:
- **UNKNOWN**: the book does not say.
- **(inf)**: my inference from the book's own text or figures. It is never a fact.
- **(gen)**: general 8259 or MC146818 knowledge. It is not from these books.

---

### C0. Probe hazards common to all of them (read first)

1. **The 22h index port on a board that aliases the master PIC (gen).** The OPTi and ALi index writes then land on the 8259 at 20h.
   - Any byte with bit 4 = 1 is an **ICW1**, which starts a PIC re-initialisation. That covers OPTi 281/283 indices 10h–16h and the M6117D unlock index **13h**.
   - Bytes 20h–27h are OCW2 with EOI=1, a **non-specific EOI**. That covers OPTi 291/391 indices 20h–27h.
   - Bytes 28h–2Fh are OCW3, and **2Ch is a poll command**.
   - So no 22h-based probe is safe until something else (for example the BIOS ROM) has narrowed the chipset family.
2. **xx72h/xx73h on a board that decodes only A9–A0.** These ports are the WD76C10 register file and FE3001's 072h/073h, and on such a board they alias **072h/073h**.
   - On the WD76C10 itself, "070–07E even" is the RTC address register and "071–07F odd" the RTC data register, using A9–A0 only (WD91 p.15, p.50).
   - So **a write to F073h or 073h on a non-WD, non-FE board writes the CMOS byte at the current RTC index.**
   - A read of 073h reads it. Reading RTC register C clears its flags (gen).
   - **Mitigation (inf):** before any xx72/xx73 access, write port 70h with a benign index such as 0Dh, which is read-only on an MC146818 (gen), keeping the current NMI bit.
3. **FE3600 registers are write-only.** An FE3600 board receiving a WD probe write to 073h stores bits 7, 6, 3, 2 of the byte into whatever R-register the 072h pointer last selected, if the command registers are unlocked. The DA unlock byte gives value 12 (inf, from FE01 p.19 and FE01A p.24).

---

### C1. OPTi 82C281/82C282 (386SX cache, 1991)

**1. Access.**
- Index port 22h, data port 24h: "Every access to port 24H must be preceded by a write to port 22H even if the same register data is being accessed again" (281 p.8).
- There are "seven bytes of configuration register" (p.8), indices 10h–16h (p.9–13).
- Registers are readable: index 10h bits 7:6 are "read only" (p.9). No lock or unlock is described.
- Reset defaults of index 10h bits 5–0 come from strapping pins MA0–5 (p.8).

**2. Identification.**
- Index 10h bits 7:6 hold the "82C281/2 revision number. 00 is the initial number. These two bits are read only" (p.9).
- There is no vendor or device ID. On a board without this chipset, a write to 22h hits whatever decodes there (C0.1).

**3. Bus timing.**

| reg | bits | field | encoding (verbatim) | default |
|---|---|---|---|---|
| 14h | 0 | AT Clock Select | "A 0 sets ATCLK2 = CPUCLK2 / 6 / A 1 sets ATCLK2 = CPUCLK2 / 4" | 0 |
| 14h | 1 | Enable Turbo Switch Function | 1 enables the TURBO pin | 0 |
| 14h | 2 | Slow refresh | "Setting this bit to a 1 will cause a refresh request to occur every 95.5 micro-seconds" (normal 15.9 µs) | 0 |
| 10h | 5 / 4 | local DRAM read / write wait states | "1=2 wait states, 0=1 wait state" | strap |

All on p.12 and p.9. AT bus clock text: "can be programmed as CLK2/6, which is default, or CLK2/4" (p.6).

- **No 8-bit or 16-bit AT wait-state, command-delay or I/O-recovery field** is documented: UNKNOWN or fixed.
- The OWS# pin is "Zero Wait State Signal from AT Channel" (p.15).

**4. Hazards.**
- 14h bit 6 is "Keyboard Reset Control - if active, a HLT instruction must be executed before the 82C281/2 generates a CPU RESET" (p.12).
- 14h bit 4 is "Fast Non-Maskable Interrupt request" (p.12).
- Port 92h bit 0 is Fast CPU Reset (p.14).
- A turbo-low TURBO pin "forces the cpu to run at the current AT bus speed" (p.6).

**5. BIOS practice.** UNKNOWN.

---

### C2. OPTi 82C283 (386SX system controller, 1994)

**1. Access.**
- Index port 22h, data port 24h. "**The index resets after every access**; therefore every data access (via Port 24h) must be preceded by a write to Port 22h, even if the same register is being accessed on consecutive occasions. All reserved bits are set to zero by default" (283 p.11).
- The book says "seven Configuration Registers" (p.11) but **documents only five, 10h–14h** (p.11–13).
- The registers are typed R/W and RO (p.11–13). No lock.

**2. Identification.**
- Index 10h bit 7 is RO: "82C283 revision number: 0 = Revision A 1 = Revision B" (p.11).
- Bit 6 is "Revision A: Reserved – Must always = 0. Revision B: 0 = Non-pipelined 1 = Pipelined" (p.11).
- There is no device ID.

**3. Bus timing.** Index 14h, Miscellaneous Control (p.13):

| bit | default | verbatim |
|---|---|---|
| 0 | 0 | "AT clock select: 0 = ATCLK2 = CPUCLK2/6 1 = ATCLK2 = CPUCLK2/4" |
| 1 | 0 | "Turbo switch function: Turning on this bit enables the turbo switch function." |
| 2 | 0 | "Slow refresh mode: ... generated internally every 15.9µs. Setting this bit to a 0 will cause a refresh request to occur every 63.6µs." The book contradicts itself here: the default is 0, and the book's section 4.3.5 (p.10) gives 15.9 regular and 63.6 slow. Polarity is UNCERTAIN. |
| 3 | 0 | Rev. B: parity enable. Rev. A: "Must be programmed to 0" |
| 4 | 0 | "Rev. A: Reserved - Must be programmed to 0. Rev. B: 0 = ATCLK set with Register 14h, bit 0 (Default) 1 = ATCLK = CLK/8 for 33MHz operation" |
| 5 | 0 | Master byte swap enable |
| 6 | 0 | Keyboard reset control (HLT required) |
| 7 | 0 | Zenith mode |

- The ATCLK pin is "ATCLK = CLK2/6 (default), ATCLK can be set to CLK2/4" (p.6). The AT bus controller supports "CLK2/6 (default) or CLK2/4" (p.10).
- **No AT wait-state, command-delay or recovery field is documented**: UNKNOWN.
- Index 10h bits 5/4 are local DRAM read/write wait states, 0 or 1, default 1 (p.11).
- **Derived (inf):** a 386SX-16 has CLK2 = 32 MHz, so /6 gives 5.33 MHz and /4 gives 8 MHz. At 25 MHz, /6 gives 8.33 MHz.

**4. Hazards.**
- 14h bit 6: the keyboard-reset HLT gating.
- Rev A reserved bits must be written 0, so read-modify-write is required.
- **Disagreement:** 86Box's `opti283.c` has a register 01h reached via port 23h and an index reset to FFh. **Neither is in the book.**

**5. BIOS practice.**
- Turbo: "If the keyboard controlled turbo switching is desired, the TURBO pin should be kept low and the turbo configuration bit should be toggled" (p.10).

---

### C3. OPTi 82C291 (386SX write-back, 1994)

**1. Access.**
- "13 Configuration Registers ... Port 22h is used as the Index Register and Port 24h is the Data Register ... **The index resets after every access** ... All reserved bits are set to zero by default and must be set to zero for future purposes" (291 p.19).
- The registers are 20h–2Ch, R/W or RO (p.20–25). No lock.

**2. Identification.**
- Index 20h bits 7:6 are RO, default 00, "82C291 revision number" (p.20).
- "I/O Configuration Register 20h[7:6] contains a 2-bit read only value which indicates the revision level ... allows the revision level of the 82C291 to be verified by software" (p.17).
- Index 29h bits 7:4 are "Reserved", default **1010** (p.24). Whether that reads back is UNKNOWN; it is a fingerprint candidate only.

**3. Bus timing.** Index 20h, Revision/AT Bus Configuration (p.20):

| bits | default | verbatim |
|---|---|---|
| 5:4 | 00 | "Back-to-back I/O recovery time: 00 = 3 ATCLKs between I/O cycles, 01 = 4, 10 = 5, 11 = 6 ATCLKs between I/O cycles" |
| 3 | 0 | Slow refresh mode, 1 = enable |
| 2 | 0 | Hidden refresh mode, 1 = enable |
| 1:0 | 00 | "AT clock selection: 00 = ATCLK = CLK2 divided by 10, 01 = ... by 8, 10 = ... by 6, 11 = ... by 4" |

Index 21h, System Control (p.20):

| bit | default | verbatim |
|---|---|---|
| 7 | 0 | AT bus master byte swap |
| 6 | 1 | "ALE generation for each AT bus cycle: 0 = Disable, a single ALE will generate during bus conversion cycles; 1 = Enable, multiple ALEs" |
| 5 | 0 | "Keyboard fast reset emulation control: 0 = Enable, a 'Halt' instruction is required before a fast CPU reset ... 1 = Disable, fast CPU reset will be generated directly after the 'FE' I/O write" |
| 4 | 0 | "**AT cycle additional wait state**: 0 = Disable, standard AT cycle; 1 = Enable, inserts one extra wait state in standard AT bus cycle" |
| 1 | 0 | local-device RDY synchronisation |
| 0 | 0 | parity checking (1 = disable) |

- Fixed timing: "Typically, the wait state for an AT 8/16-bit transaction is **5/1** respectively. The command cycle is extended when CHRDY is pulled low ... or the cycle is terminated when ... N0WS# ... is active" (p.16). There is no separate 8-bit wait-state or command-delay field.
- Features list: "Synchronous AT bus clock with programmable clock division options: CLK2/4, /6, /8, or /10"; "Zero or one wait state options for 16-bit AT bus cycles" (p.1).
- ATCLK pin: "derived by dividing the CLK2 input by the setting specified in ... Register 20h[1:0]" (p.6, p.13).
- Index 22h bits 7:6 and 5:4 are DRAM read/write wait states 0–3, default 11 (p.21).
- **Derived (inf):** a 33 MHz CPU has CLK2 = 66 MHz, so /8 gives 8.25 MHz. At 40 MHz, /10 gives 8 MHz.

**4. Hazards.**
- The default **00 is the slowest divisor** (/10), so a BIOS must have raised it.
- Index 21h bit 5 = 1 makes a port-64h FEh write reset without a HLT.
- Port 92h bit 0 is a fast reset, which "remains set until written again" (p.25).
- Reserved bits must be written 0.

**5. BIOS practice.** UNKNOWN. No recommended table is given.

---

### C4. OPTi 82C391/82C392 "386WB" (386DX, 1990)

**1. Access.**
- "twelve configuration registers inside the 82C391 ... Port 22h contains the address of the index register and port 24h is used as the data register. Every access to port 24h must be preceded by a write to port 22h even if the same register is being accessed again. All reserved bits are set to zero by default and must be set to zero" (391 p.17).
- Indices are 20h–2Bh (p.17–25).
- The 82C392 DBC snoops index 21h: "Control Register Index 21h (write only). Bit 7-? is a duplication of control register index 21h of 82C391" (p.32).

**2. Identification.**
- Index 20h bits 7:6 are "Revision of 82C391 and is read-only", default 00 (p.17). There is no device ID.
- Index 20h bits 5:4 are **not described at all** (p.17, checked against the page image).

**3. Bus timing.**
- **ATCLK is not software-programmable**: "ATCLK can be derived from CLK2IN/6 or CLK2IN/8 depending on the state of 'BCLKS' input pin ... If BCLKS is tied to low, CLK2IN/8 is selected, otherwise CLK2IN/6" (p.7). BCLKS is pin 14 (p.12).
- The reference board's JP3 says "AT Bus clock is CPU clock divided by 6 / divided by 8" (p.41). It says "CPU clock" where the chip text says CLK2IN, which is a wording disagreement.
- Index 20h bit 2: "Extra AT Cycle Wait State Enable. Insert one extra wait state in standard AT bus cycle. 0 = disable 1 = enable", default 0 (p.17).
- Index 20h bit 3: "Single ALE Enable – SYSC will activate single ALE instead of multiple ALEs during bus conversion cycle", default 0 (p.17).
- Features: "AT bus clock = CLK2IN/8 or CLK2IN/6"; "0 or 1 wait state for 16-bit AT bus cycle" (p.5).
- There is **no 8-bit wait-state, command-delay or I/O-recovery field**: UNKNOWN or fixed.
- The NOWS# note says "the system BIOS ROM is treated as AT zero wait state cycle" (p.16).
- Index 25h DRAM read wait states are additional to a base of 3 (01 = 0, 10 = 1, 11 = 2, default 11). Write wait states are additional to a base of 2 (000 = 0, 010 = 1, 100 = 2, 110 = 3, default 110) (p.21).

**4. Hazards.**
- "CPURST also can be activated by programming bit 0 of indexed register 20h from 0 to 1, then executing a 'HALT' instruction" (p.7).
- Index 20h bit 0 is "Fast Reset Enable" and bit 1 is "Keyboard and Fast Reset Control ... requires 'Halt'" (p.17).
- Index 21h bit 6 sets the fast-keyboard-reset delay: 0 gives a pulse "2 us later" (p.18).
- Index 22h bit 7 "ROM Enable" and index 26h shadow bits move ROM and RAM.

**5. BIOS practice.** Only the reference-board jumpers (p.41). There is no register recipe.

---

### C5. UMC UM82C231 (UM82C230 286 set)

**1. Access.** **There are no software configuration registers.**
- "The appropriate number of wait states are inserted, as programmed by strap pins externally" (U231 p.15).
- Memory configuration comes from the MS(2:0) strap pins (p.13).
- The only I/O the sheet names is F0h/F1h, the 287 busy-clear and reset (p.7, p.14).

**2. Identification.** None by I/O.

**3. Bus timing.**
- "ATCLK is also derived from CLK4IN at 1/3 of the CPU clock frequency" (p.12). The ATCLK pin is "generated from CLK4IN/6" (p.5), which is consistent because CLK4IN is twice the CPU clock.
- The AT state machine "uses BCLK which is twice the frequency of AT system clock ATCLK" (p.12).
- "Typically, the wait state for an AT 8/16 bit transaction is **5/3** respectively. The command cycle is extended when CHRDY is detected inactive, or is terminated when ... NOWS# ... is active" (p.13).
- Nothing is programmable.

**4–5.** Not applicable.

---

### C6. UMC UM82C212 (UM82C210 286 set, memory controller)

**1. Access.**
- "twelve 8-bit registers, R21–R2C ... the index access scheme is used. To access a particular register, the index address is placed at address port and the data is located at data port" (U212 p.5).
- **The port numbers are not given.**
- Indices are 64h–6Fh (p.5–11).
- The layout matches C&T NEAT's 82C212 bank RB0–RB11 at index 64h–6Fh through 22h/23h (NEAT p.47). So 22h/23h is **(inf)**.

**2. Identification.**
- R21 is index 64h: "Bits 0-4 Reserved. 5,6 Revision number. 7 Chip identifier" (p.5). No values are given.
- NEAT's identical register reads "7 NEAT memory controller identifier. 0 = 82C212. 6, 5 82C212 revision number. 00 = initial" (NEAT p.47).
- **A UMC board and a NEAT board cannot be told apart from this register on the books' evidence.**

**3. Bus timing.**
- R28 is index 6Bh: "0,1 ROM access wait state. 2,3 EMS memory access wait states. 5 RAM access wait states" (p.8). The encodings are UNKNOWN.
- **AT bus clock, AT wait states and command delays would be in the UM82C211 bus controller, which is not supplied.** UNKNOWN.

**4. Hazards.**
- R22 (65h) enables and disables ROMs, R23 (66h) system-board RAM, and R2C (6Fh) bit 1 "CPUA20 enable" (p.5–6, p.11).

**5.** UNKNOWN.

---

### C7. Western Digital FE3600 family (FE3001 / FE3001A + FE3010 / FE3021 / FE3031)

The WD 1991 book has chapters FE3001, FE3001A, FE3010C, FE3021, FE3021A, FE3031 and FE3031A (TOC p.5). **Only the FE3001, FE3001A and FE3600B overview were available.** The FE3021, the memory and chip-select controller, was not.

#### C7.1 Configuration access

| port | dir | content | source |
|---|---|---|---|
| **063h** | **W only** | Speed Select. Bit 2: "0 = CPU speed – CLK16, 1 = CLKHS". Bit 3: "0 = DMA speed – standard, 1 = high speed (2 × standard)". Bit 6: "1 = stop CPU & DMA clocks". Bit 7: "**0 = UNLOCK & WRITE REGISTERS, 1 = LOCK COMMAND REGISTERS**". Bits 0, 1, 4, 5 are unused. | FE01 p.17; FE01A p.21; FE36 p.7 |
| 063h | reset | "Bits 2, 3, and 6 in the register are cleared and **bit 7 is set by system reset**." The part is therefore **locked at reset**. | FE01 p.17; FE01A p.21 |
| 063h | rule | "The lock bit must be reset and D7 must be low to change speed or stop clocks." | FE01 p.17 |
| **072h** | **W only** | Command register pointer. Bits 7, 6, 3, 2 = r3, r2, r1, r0 (bits 5, 4, 1, 0 are X). | FE01A p.24 (figure); FE01 p.19; FE36 p.7–8 |
| **073h** | **W only** | Value. Bits 7, 6, 3, 2 = d3, d2, d1, d0. | FE01A p.24; FE36 p.8 |

- FE36's worked example: "If bits 2, 3, 6, and 7 are 0, 1, 0, and 1 ... the address is 10 (decimal)" (FE36 p.8).
- There is **no auto-increment and no index reset**. The register written is "the register which is loaded with the next write to address 073H" (FE01 p.19).
- **Readability:** for port 061h "this is the only time the FE3001 will drive the data bus. **All other I/O locations are write only.** Also, only bits 6 and 7 will be driven during the read" (FE01 p.15; same text FE01A p.17). The FE36 overview says 063h, 072h and 073h are write-only (FE36 p.7).
- **Decode:**
  - The FE3001 sees only address lines A0, A1 and A3, plus two decodes from outside: NMICS "NMI port enable decode (07XH). Also used for programming bus control registers" and PBCS "Port B chip select decode (061H–06FH, ODD)" (FE01 p.11).
  - **(inf)** Because A2 is not an input, 076h/077h probably alias 072h/073h, and 067h aliases 063h.

#### C7.2 Identification

- **There is none by I/O**, because nothing but 061h bits 7:6 is readable (FE01 p.15) and those are the standard Port B parity and I/O-check bits (FE01 p.17).
- **Any I/O "probe" is a write**, and a write to 063h also sets CPU clock, DMA clock and the stop bit, so it cannot be done blind:
  - 063h is write-only, so the current CLK16/CLKHS state cannot be read back.
  - An unlock write therefore **re-selects the CPU clock**. That has to be decided by something else, such as a measured CPU speed against the PIT (inf).
  - Writing bit 6 = 1 stops the clocks, and only an external RESCPU pulse restarts them (FE01 p.13).
- On a non-FE board, 072h/073h land on the RTC (C0.2).
- **Identification must come from outside I/O**: the BIOS ROM or the board identity.

#### C7.3 Bus timing

**Clocks** (FE01 p.12–13; FE01A p.13–14; FE36 p.6–7):
- PROCLK is CLK16, or CLKHS when 063h bit 2 = 1. CLKHS/2 is used for CLK16 when CLK16 is tied high.
- **SYSCLK = PROCLK/2 at low speed and PROCLK/4 at high speed.** At high speed SYSCLK "is brought into sync with the PC/AT bus at the end of ALE" (FE01 p.13).
- **FE3001A only:** "When R13 bit 7 is set, SYSCLK is PROCLK ÷ 6" and DMACLK is PROCLK/12 at high speed (FE01A p.14, p.13).
- DMACLK is PROCLK/4 at low speed and PROCLK/8 at high speed, and twice those with high-speed DMA (FE01 p.12).

**Command timing, Table 3-1** (FE01 p.18, checked against the page image; FE01A p.23):

| R | bits | function | default | range |
|---|---|---|---|---|
| R0 | 2 | BALE delay from ALE leading edge (PROCLKs) | 0 | 0–3 |
| R1 | 2 (FE01A: "4") | BALE width (PROCLKs) | 1 | 1–3 |
| R2 | — | Not used | | |
| **R3** | 4 | **8-bit memory, 8/16-bit I/O – command delay** | 1 | 0–15 |
| **R4** | 4 | **8-bit operation – wait states** | **4** | 0–15 |
| **R5** | 4 | **16-bit I/O operation – wait states** | 1 | 0–15 |
| R6 | 4 | 16-bit memory operation – command delay | 0 | 0–15 |
| R7 | 4 | 16-bit memory operation – wait states | 1 | 0–15 |
| R8 | 4 | Minimum number of wait states when ZEROWS is asserted | 0 | 0–15 |
| R9 | 4 | On-board 16-bit read cycle – wait states | 1 | 0–15 |
| R10 | 4 | On-board 16-bit write cycle – wait states | 1 | 0–15 |
| R11 | — | Not used | | |
| R12 | 4 | On-board I/O operation – command delay | 1 | 0–15 |
| R13 | 4 | (FE3001A only) Enhanced Features Enable | OFF | N/A |

Footnotes, verbatim (FE01 p.18):
- "Command delay is number of PROCLKs from end of Ts."
- "**Each wait state is two PROCLKs.**"
- "One wait state may be added in high speed mode for synchronization."

**Semantics.**
- **R3:** "This register controls the command delay for 8-bit memory and 8/16-bit I/O operations. Command delay is defined as the number of PROCLK cycles from the **trailing edge of ALE** to the start of the command" (FE01 p.19). The table footnote says "from end of Ts" instead. Figure 3-1 draws C from the ALE trailing edge, so the two agree in practice.
- **Does lowering R3 help? No, not for cycle length (inf from Figure 3-1, FE01 p.18).**
  - The wait-state interval D is drawn from the end of Tc to the end of the cycle, independent of C. The cycle is Ts + Tc + R4 × Tw (+ sync), whatever R3 is.
  - A smaller R3 only starts the command earlier, so the command is longer and the address setup shorter.
  - **It is also a correctness hazard for the IDE path.** "I/O cycles use 16-bit timing when IOCS16 is driven low before the I/O command (YIOR or YIOW) is programmed to go active. Otherwise, all cycles use 8-bit timing" (FE01 p.14; FE01A p.15). A smaller R3 shrinks the window an IDE or 16-bit card has to assert IOCS16, and if the card misses it **the cycle is run as 8-bit**.
  - The memory equivalent is MEMCS16 "before either the memory command ... is programmed to go active or BALE falls, whichever is earlier" (same pages).
- **R0/R1 (BALE):** "BALE delay is defined as the number of PROCLK cycles from the leading edge of ALE. BALE width is the width in PROCLK cycles" (FE01 p.19).
- **R4:** "the number of CPU wait states required for these operations" (FE01 p.19). Two PROCLKs each, which is one 286 T-state (inf).
- **R5 (16-bit I/O, the IDE path):** "controls the number of wait states for 16-bit I/O cycles. These cycles are indicated by the assertion of IOCS16" (FE01 p.19). The command delay for these cycles is R3, not R6.
- **R6/R7:** 16-bit memory "indicated by the assertion of MEMCS16. Command delay is defined as the number of PROCLK cycles from the trailing edge of ALE" (FE01 p.19).
- **R8:** "the minimum number of wait states which must occur before the assertion of the ZEROWS signal can terminate a cycle" (FE01 p.19).
- **SYSCLK edge rule:** "the FE3001 contains circuitry to ensure that an AT bus cycle will end (command goes high) on a rising edge of SYSCLK and will add a wait state if needed to enforce this. This synchronization circuitry will override register programming and the IOCHRDY and ZEROWS inputs" (FE01 p.14; FE01A p.15).
  - The rule applies to **every AT bus cycle**, 8-bit and 16-bit, not only 8-bit.
  - Interrupt-acknowledge cycles "follow the same command timing as an 8-bit AT bus cycle" (same pages).
- **R13 (FE3001A):** "Bit 2 provides one extra CPU cycle (2 CPUCLKs) data hold time for AT bus cycles at high speed. Bit 3 must be programmed to '0'. When Bit 6 is set, CS287 will be treated as ROMCS for all memory cycles. Bit 7 enables SYSCLK = PROCLK ÷ 6 and DMACLK = PROCLK ÷ 12 option at high speed" (FE01A p.26).

**Recommended values, Appendix A.** These are "based on emulating an 8 MHz IBM PC/AT Expansion Bus". The table titles say "system clock speed". **(inf)** That means CPU MHz: at 16 MHz, R4 = 10 gives 12 T × 62.5 ns = 750 ns, which equals the 8 MHz AT's 6 T × 125 ns.

| R | FE3001 6.25 | 8 | 12.5 | 16 | FE3001A 6.25 | 8 | 12.5 & 16 | 20 |
|---|---|---|---|---|---|---|---|---|
| R0 | 0 | 0 | 1 | 1 | 0 | 0 | 1 | 2 |
| R1 | 1 | 1 | 2 | 2 | 3 | 3 | 6 | 5 |
| R3 | 1 | 1 | 4 | 4 | 3 | 3 | 7 | 9 |
| R4 | 4 | 4 | 8 | **10** | 5 | 5 | 12 | 15 |
| R5 | 1 | 1 | 4 | 4 | 2 | 2 | 6 | 8 |
| R6 | 0 | 0 | 2 | 2 | 2 | 2 | 6 | 6 |
| R7 | 1 | 1 | 4 | 4 | 2 | 2 | 6 | 8 |
| R8 | 0 | 0 | 2 | 2 | 1 | 1 | 4 | 5 |

Sources: FE01 p.56–57; FE01A p.64–65.
- FE3001A R1 = 6 and R1 = 5 exceed the table's own R1 range of 1–3 (FE01A p.23). That is an internal contradiction.
- The FE3001A values are **much more conservative**.
- The owner's "R4 = 10 at 16 MHz" is the **FE3001** table.

#### C7.4 Hazards

- **Programming order** (FE01 p.55; FE01A p.63), verbatim: "setup the FE3001 registers in a particular order so that BALE and commands do not become too short during the programming process".
  - "Selecting High Speed CPU Clock: R5, R6, R7, R8, R9, R10, R12, R4, R3, R1, R0."
  - "Selecting Low Speed CPU Clock: R5, R6, R7, R8, R9, R10, R12, R0, R1, R3, R4."
- **BALE erratum:** "If the sum of R0 and R1 is 3 or greater, then an internal BALE logic error in first production FE3001 parts may cause BALE to remain high from a previous on-board cycle and fall one PROCLK cycle too early. Widening BALE by one clock accounts for this" (FE01 p.55; FE01A p.63).
- **MEMCS16 decoders:** "Some adapter boards decode the SA0–SA19 address lines to generate MEMCS16 ... widening BALE and delaying the commands ... provides relaxed MEMCS16 setup time" (same pages).
- **Program the registers before switching up:** "Before switching to high speed operation, it is necessary to program the registers for proper bus emulation" (FE01 p.12).
- **Power-up state:** "On power-up, these registers are loaded with values to run the system with a 16 MHz PROCLK (8 MHz system) with full AT compatibility" (FE01 p.12).
- **Write-only:** every register is write-only, so they cannot be saved or restored. A tool must re-derive the whole set and own the 063h speed bits.
- **Sleep:** 063h bit 6 sleeps the machine, and only RESCPU wakes it (FE01 p.13).

#### C7.5 BIOS practice

- Appendix A, the order above, and "a 16 MHz system can be made to exactly match the bus timing of an 8 MHz system" (FE01 p.12).

#### C7.6 Disagreements inside the WD material

| | FE3600B overview (FE36 p.8) | FE3001 Table 3-1 (FE01 p.18) |
|---|---|---|
| R8 | "16-bit memory operation – wait states", 0 | ZEROWS minimum, 0 |
| R9 | "Local DRAM read – wait states" default **0** | on-board read default **1** |
| R11 | "Local DRAM write – cmd delay" 0 | "Not used" |
| pointer | "one of 14 registers" (p.7) | "one of 11 registers" (p.19) |

- FE36 says "Bit 7 is used to unlock and write other registers in the **FE3000A**" (p.7).
- The emulator survey's facts (write-only, 063h bits, 72h/73h nibble bits, R4 = two PROCLKs) all **agree** with the books.

---

### C8. Western Digital WD76C10 / WD76C10A (WD75C10, LP, LV)

#### C8.1 Configuration access

- **Registers are 16-bit**, at full 16-bit addresses xx72h. "In most cases, the registers are addressed by all 16 address bits ... If the address is expressed as a four digit number, all 16 address bits are used" (WD91 p.12; WD93 p.16).
- Three-digit ports such as 070–07F use A9–A0 only (same pages).
- **The access width for the xx72 registers is not stated.** They are documented as bits 15–00, so word I/O is (inf).

**Lock.**
- "With the exception of the EMS Registers at port E072 and E872, all registers located at Ports 1072 through F872 [WD93: '1072H through FC72H', and E472H also excepted] are locked and inaccessible until unlocked by performing an **eight bit I/O write of DA to the Lock/Unlock Register at Port F073**. Writing anything other than DA locks the registers" (WD91 p.12; WD93 p.16).
- F073h is write-only. 11011010 unlocks. "Anything other than 11011010 locks the registers. Any attempt to access a locked register I/O port address goes to the AT bus rather than the locked register" (WD91 p.13; WD93 p.17).
- **Default at RSTIN:** WD93 p.17 gives "None" for F073's L/UL field, and neither book states whether the registers are locked after reset. (inf) Locked, since a locked register is the one the text describes as needing the unlock.

**Status.**
- "The lock/unlock status can be determined by reading the Lock/Unlock Status Register at Port FC72 twice. If the T bit (bit 15) toggles, the registers are unlocked. **If the registers are locked, the read cycle is directed to the AT bus, and the data is undetermined**" (WD91 p.12; WD93 p.16).
- FC72h bit 15 "T, Toggle. Changes state after every read of this port". Bits 11–3 are DMA mask and parallel-port direction shadows (same pages).

**Register index:**
- 1072 CPU clock, 1872 bus timing and power-down, 2072 refresh and serial/parallel chip selects, 2872 chip selects, 3072–6872 memory, 7072–C872 PMC, 9872 diagnostic, A072 delay line, F072/F472 48 MHz oscillator, F872 cache flush (WD91 p.15).
- WD93 adds A872, B072, D072, D472, D872, DC72 and E472 (WD93 p.19–20).

#### C8.2 Identification

- **Version (needs the unlock):** 9872h bits 15:13 VER.
  - 000 WD76C10 Rev A, 001 Rev B, 010 Rev C, 011 Rev D, 100 Rev E, 101 Rev F, **110 WD76C10A Rev A**, 111 Extended Versions.
  - "If version seven is read, it is necessary to read the device type (DEV) from Port Address D472H, then the secondary version number (SVER) from Port Address A872H" (WD93 p.101).
  - D472h bits 14:13 DEV: 00 WD76C10A, 01 WD7710, 1x Reserved (WD93 p.94, p.101).
  - A872h bits 15:12 SVER: 0000 = A, 0001 = B, ... (WD93 p.101, p.104).
  - 9872h bit 7 SX: "0 = 80286 processor was detected, 1 = 80386SX" (WD93 p.102).
- **A read-only candidate (WD76C10A only):** E472h, the "Port 70H Shadow Register", is read-only and "**may be accessed without first being unlocked**" (WD93 p.95).
  - Bit 15 is CLK32K, bit 14 REFDT, which "changes state on each refresh". Bit 13 is INTRQ and bit 12 NODMA.
  - "Bits 11–08 Reserved. Currently defaults to 0000, but is subject to change."
  - "Bits 07 through 00 represent the state of the Disable Non-maskable Interrupt and Real-Time Clock Address as set by the last write to Port Address 0070H."
  - E472 arrived with the "A" documentation: Revision History 2, 4/10/91 to 7/1/91, adds "A872, 8072, D472, D872, DC72 and E472" (WD93 p.199). A **pre-A WD76C10 may not have it** (inf).
- **Safest probe sequence (inf, assembled from the above):**
  1. Write port 70h = (current NMI bit) | 0Dh. This is a normal RTC operation, and it makes any stray 071h/073h access hit RTC register D, which is read-only (gen).
  2. Read word **E472h** several times, more than 15 µs apart. On a WD76C10A the low byte equals the 70h value, bits 11–8 are 0, and bit 14 toggles across refreshes.
     - On a non-WD board the read aliases 072h/073h. The high byte is then the static CMOS[0Dh], and the low byte is undefined, or the index on chipsets that read 70h back.
     - **Bit 14 toggling** is therefore the discriminator. The read has no side effects.
  3. Only if step 2 passes, write DAh to F073h, then read FC72h twice and require bit 15 to toggle. Then read 9872h for VER.
  4. Re-lock by writing 00h to F073h.
  - Step 3 is a write, which on a non-WD, non-FE board lands on the CMOS RAM at index 0Dh (C0.2), and on an FE3600 board on 073h (C0).
  - Whether a write to F073h *also* writes the WD's own RTC data register (071–07F odd, A9–A0 decode) is **UNKNOWN**. Index 0Dh covers that case too.
- **Disagreement on FC72h:**
  - 86Box treats FC72h as readable while locked, with bit 15 always toggling (survey).
  - Both books say a locked read goes to the AT bus.
  - However, WD93's Revision History 4 (7/23/91 to 8/15/91) records "Table 2-1 FC72 Lock Status changed from no to yes" (WD93 p.204). An intermediate document listed FC72 as not lockable, and silicon may follow either. **Unverified.**

#### C8.3 Bus timing: port 1872h "Numeric Processor Busy, Bus Timing, And Power Down" (R/W after unlock)

Sources: WD91 p.32–34 (checked against the page images); WD93 p.39–41.

| bits | name | default at RSTIN | encoding (verbatim) |
|---|---|---|---|
| 15 | NP_BSY | 0 | WD91: "0 – Forces an early BUSYCPU when the co-processor address is F8 through FF and IOW (Default); 1 – Numeric processor busy". WD93: "NP_BSY **must be set** for systems using an 80286 CPU where the CPU runs faster than the AT bus" yet "NP_BSY = 0 – Force an early BUSYCPU ... (Default value). NP_BSY = 1 – Normal BUSYCPU assertion". **WD93 contradicts itself.** |
| 14 | PRO_PD (LP only) | 0 | 1 = processor power down at the next Halt |
| 13 | FPD (LP only) | 0 | 1 = full power down on Halt |
| 12 | — | none | "Ignored ..., may be 0 or 1" |
| 11:10 | **BUS_MOD** | 00 | "00 – Bus logic uses BCLK2 divided by 2 (Default). 01 – Bus logic uses BCLK2 divided by 1. 10 – Bus logic uses CPUCLK divided by 2. 11 – Bus logic uses CPUCLK divided by 1." |
| 9:8 | BRQ_DEL | 00 | "00 – 1 Bus clock delay (Default). 01 – .5 Bus clock delay. 10 – No clock delay. 11 – Reserved." |
| 7:6 | BAK_DEL | 11 | "00 – No delay. 01 – -.5 Bus clock delay. 10 – -1 Bus clock delay. 11 – +.5 Bus clock delay (Default)." WD93 adds: "referenced to the trailing edge of the AT command strobe" and "determines AT bus hold time for the data and address". |
| 5 | **WSI16** | 0 | "0 – 1 Bus clock wait state (Default). 1 – 2 Bus clock wait state". This is the **16-bit I/O, IDE path**. |
| 4 | WSM16 | 0 | "0 – 1 Bus clock wait state (Default). 1 – 2 Bus clock wait state" |
| 3:2 | **WSI8** | 10 | "00 – 2, 01 – 3, 10 – 4 (Default), 11 – 5 Bus clock wait state" |
| 1:0 | **WSM8** | 10 | "00 – 2, 01 – 3, 10 – 4 (Default), 11 – 5 Bus clock wait state" |

**Clock meaning.**
- "SYSCLK is divided by two regardless of the mode selected by BUS_MOD". So **SYSCLK = (selected clock ÷ BUS_MOD divisor) ÷ 2**.
- "if BCLK2 is 16 MHz at power up ... a SYSCLK clock rate of 4 MHz until programmed to mode 01. In mode 01, the SYSCLK rate is 8 MHz for a BCLK2 of 16 MHz. Both mode 00 and 01 are asynchronous and require the appropriate synchronization delays ... In modes 10 and 11, the SYSCLK is synchronous with the CPUCLK and synchronization delays are eliminated. The bus clock mode may need to be reprogrammed when the processor clock changes" (WD91 p.33).
- WD91 says async mode "allows CPUCLK to be **slower** than SYSCLK", where WD93 says "**faster**" (WD93 p.40).
- Timing notes: "AT bus clock = CPUCLK ÷ 1 ... BUS_MOD = 11" and "CPUCLK ÷ 2 ... BUS_MOD = 10" (WD93 p.148, AC timing notes 2–3); and "SYSCLK will always be one half or one fourth the value of the selected input clock" (WD91 p.25).

**Table 5-2, the recommended values** (WD91 p.34; WD93 p.41). The CPU speed is the processor rate.

| CPU | speed | AT bus | mode | BUS_MOD | BRQ_DEL | BAK_DEL |
|---|---|---|---|---|---|---|
| 286 | 25 | 8 | ASYNC | 0X | 00 | 00 |
| 286 | 20 | 8 | ASYNC | 0X | 01 | 01 *(WD93 only)* |
| 286 | 20 | 10 | SYNC | 10 | 10 | 10 |
| 286 | 16 | 8 | SYNC | 10 | 10 | 10 |
| 286 | 12.5 | 8 | ASYNC | 0X | 01 | 10 |
| 286 | 10 | 10 | SYNC | 11 | 10 | 10 |
| 286 | 8 | 8 | SYNC | 11 | 10 | 10 |
| 386SX | 25 | 8 | ASYNC | 0X | 01 | 00 |
| 386SX | 20 | 10 | SYNC | 10 | 10 | 10 |
| 386SX | 20 | 8 | ASYNC | 0X | 01 | **10 (WD91) / 00 (WD93)** |
| 386SX | 16 | 8 | SYNC | 10 | 10 | 10 |
| 386SX | 12.5 | 8 | ASYNC | 0X | 01 | 10 |

**What 1872h does not have.**
- **No separate command-delay field and no I/O-recovery field are documented** in either book: UNKNOWN.
- The wait-state unit is the **bus clock (SYSCLK)**, not a CPU T-state.

**Port 2872h, "RTC, PVGA, 80287 timing, disk chip selects"** (R/W after unlock; WD91 p.73–74; WD93 p.80–81):
- **bit 14 FST_VGA** (default 0): "I/O cycles to ports 3C0–3CF are made with one wait state cycles" (WD91). WD93 narrows this to 8-bit ports 3C0–1, 3C4–5 and 3CE–F, and only for listed WD/Paradise VGAs.
- **bit 13 FST_SCSI** (default 0): "eight-bit accesses [to the WD33C93] with one wait state rather than four".
- **bit 6 HS_HD** (default 0): "Enabling the high speed data transfers results in hard disk, 16-bit data transfers to be performed at a compressed timing rate rather than at the compatible bus rate. ... the first data transfer is made at the compatible bus rate. Subsequent accesses to the hard disk port are made at high speed".
  - **WD91** adds "with BALE (pin 79) suppressed".
  - **WD93** adds "with IOCS16 ignored and the WD76C20 hard disk chip select remaining stable". Its note says it "**requires the use of the WD76C20 and should only be used with** Western Digital IDE drives WD-AC280, WD-AC140, WD-AC160, WD-AC2120, WD-AP4200, WD-AB130 and WD-AH260".
  - The two books **disagree on the mechanism**.
- **bit 3 HS_287:** "Normal I/O ... access to the 80287 is made with eight bit bus timing. Setting HS_287 results in 16 bit bus timing."
- **bit 15 RTC_L:** the RTC on the RA/ED bus or the expansion bus.
- **bits 1 / 0 DS_HD / DS_FLP:** chip-select **disables**. Writing 1 removes the HDD or FDC chip select, which is a hazard.
- Bit 2 is LK_PSW.

#### C8.4 Hazards

- **1072h, CPU clock control:**
  - "Changing the CPUCLK from an input (000) to an output automatically asserts the processor reset (CPU RES)."
  - Bits 1/0 (SCH/SCHH, LP only) stop the CPU clock.
  - Bits 7:4 EXT_HOLD stretch the hold after refresh (1, 2, 3, 4 µs ... up to 15 µs).
  - Bits 14:12 CLK_SPD divide the CPU clock by 1, 2, 4 or 8.
  - Default CLK_SPD is 001 and SRC 0/1. Sources: WD91 p.27–28; WD93 p.34.
- **1872h bits 14/13:** PRO_PD and FPD power down at the next HLT (LP).
- **The rule for 1872h and 2872h:** always read-modify-write, and never write either register blind.
- **Port 092h:** bit 3 LOCK_PASS can only be cleared by RSTIN, and bit 0 is HOT_RST (WD91 p.50).
- **Port 0F1h:** "The main processor is wait stated for 128 clocks when writing to this port" (WD91 p.34).
- **The unlock write and the RTC:** see C8.2. Re-lock afterwards, because a locked part routes xx72 accesses to the bus.

#### C8.5 BIOS practice

- Table 5-2 is the recipe: "Refer to Table 5-2 for the appropriate choices according to the CPU type and speed and AT bus speed employed" (WD91 p.33).
- The power-up state is BUS_MOD 00, which gives SYSCLK 4 MHz with a 16 MHz BCLK2 "until programmed to mode 01" (WD91 p.33). A BIOS must therefore program 1872h.

---

### C9. ALi M6117D (386SX SoC with an "ALi M1217B chipset" core)

"M6117D is ... microprocessor plus ALi M1217B chipset" (M61 p.2).

**1. Access.**
- "On board I/O port 22h is the index register and I/O port 23h is the data register. To read a configuration register, write the index value to I/O port 22h in advance, then read data from I/O port 23h" (M61 p.18).
- Unlock and lock, verbatim (p.18):

```
OUT 22h, 13h (Enable 13h)
OUT 23h, C5h (Unlock)
OUT 22h, XXh / OUT 23h, YYh  (Configuration can be written repeatedly)
OUT 22h, 13h
OUT 23h, 00h (Lock)
```

- Index 13h reads "C5h : Unlock configuration Register. 00h : Lock Configuration Register", default 00h, so the part is **locked at reset** (p.44).
- Port 23h is "Data of Configuration register if unlock register unlocked" (p.42). **What a locked read returns is UNKNOWN.**
- Whether the index self-resets after a data access is **UNKNOWN**. The examples rewrite 22h every time.
- The register summary is on p.41–42.

**2. Identification.**
- Index 36h bits 2:0 "Chip version (Read only) 010 : M6117D" (p.47).
- Index 34h, read-only power-on straps: bit 0 "Read 0 = C mode, 1 = D mode" (p.47).
- Index 30h bit 3 is CPU speed, read-only (p.46).
- **The safest sequence (inf):** gate on the BIOS first because of C0.1. Then write 13h/C5h, read 36h and 34h, and re-lock with 13h/00h.

**3. Bus timing.**
- **Index 1Eh, ATCLK1 Definition** (default 00h, bits 7–3 reserved), from p.45. PCLK2 is "doubled CPU clock" (p.29).

| D[2:0] | High Freq. | Normal Freq. |
|---|---|---|
| 000 | 14.318/2 | 14.318/2 (default, 7.159 MHz) |
| 001 | PCLK2/2 | PCLK2/3 |
| 010 | PCLK2/3 | PCLK2/4 |
| 011 | PCLK2/4 | PCLK2/5 |
| 100 | PCLK2/5 | PCLK2/6 |
| 101 | PCLK2/6 | PCLK2/8 |
| 110 | PCLK2/8 | PCLK2/10 |
| 111 | PCLK2/10 | PCLK2/12 |

  - The book's section 4.4.1 prints the 010 row as "PCLK2/4 PCLK2/3", a typo (p.29). Its sections 4.4.3 and 4.13 both give normal = /4 (p.30, p.45).
  - The book's example: "CPU running at 40 MHz, the PCLK2 will be 80 MHz and if we choose D[2-0] = 110 ... ATCLK = PCLK2/10, then ATCLK is 8 MHz" (p.29).
  - "Note: The 82C54 has some limitations which require ISA ATCLK set as 7.159 MHz. Please refer to Appendix C" (p.29). **Appendix C is not in this PDF.**
- The "high frequency" column applies only inside address windows (p.30, p.44–45):
  - memory set 1 at 16h–18h, memory set 2 at 19h–1Bh, and the I/O range at 1Ch–1Dh, with mask bit 1 = compare;
  - enabled by index 32h bit 7 "ISA I/O high speed" and bit 6 "ISA memory high speed" (p.46).
- **Index 33h, I/O recovery** (default 00h, p.30, p.46):
  - bits 7:4 are the period in 250 ns units, "0000 = 0 (default) ... 1111 = 3750 ns";
  - bit 3 "I/O recovery 1 = enable"; bit 2 "On-chip I/O recovery 1 = enable"; bit 0 "Must be 0".
  - "you have to enable the I/O recovery time in advance, otherwise index 33h:D[7-4] will not work ... recommends value of 500 ns" (p.29).
- **Index 36h bit 7:** "16 bit ISA cycle insert 1 wait: 0 disable, 1 enable" (p.47).
- **8-bit wait states and command delays are not documented: UNKNOWN.** "The ISA bus controller ... developed and verified by ALi's M1487/M1489 series" (p.29).

**4. Hazards.**
- **Index 20h** (default 80h): bit 7 "DRAM controller 0: disable", which takes the machine's memory away. Bit 2 enables flash ROM writes, and bit 1 self-refresh (p.45).
- **Indices 37h–3Ch, the watchdog:** 37h bit 6 is the watchdog enable, and 38h bits 7:4 = 1101 select "system reset" (p.47–48).
- **Index 31h** bit 5 controls fast reset (p.46).
- **Index 73h** is "Power control status output command", write-only (p.42).
- **Index 72h** is "Shadow I/O port for the data of port 70H" (p.42).
- Memory timing lives in 10h–12h.

**5. BIOS practice.**
- "After powering on, BIOS can detect the CPU frequency, and set the desired AT clock frequency" (p.29).
- **Disagreement:** the survey's 86Box `ali6117.c` decode of 1Eh ("7.16 MHz or PCLK2/1.5, /2, ...") **does not match** the book's normal column (/3 through /12).

---

### C10. Corrections to the emulator survey (`docs/reports/CHIPSET-SURVEY-2026-10-13.md`)

- **OPTi 82C291:** the survey had the registers stored only. The book gives the full encodings: 20h[1:0] ATCLK /10, /8, /6, /4, 20h[5:4] I/O recovery of 3–6 ATCLKs, and 21h[4] one extra AT wait state.
- **OPTi 82C391/381:** the ATCLK is **strapped** (the BCLKS pin, /6 or /8). The reset hazard is 20h bit 0 **followed by a HALT**.
- **OPTi 82C283:** index 01h via 23h and the index resetting "to FFh" are not in the book. Rev B's 14h bit 4 adds CLK/8.
- **WD76C10:** FC72h is readable only when unlocked, per the book. 1872h is fully decoded (C8.3), and the read-only E472h exists on the A part. The ID is at 9872h, D472h and A872h.
- **FE3600:** confirmed write-only, with no I/O identification. Locked at reset (063h bit 7 = 1). R5 is the 16-bit I/O (IDE) wait-state count, and R3 is its command delay. Lowering R3 does not shorten a cycle and can turn a 16-bit I/O cycle into an 8-bit one.
- **UMC:** the UM82C231 has nothing programmable. The UM82C212 is the NEAT 82C212 register bank, and its bus timing lives in the unsupplied UM82C211.
