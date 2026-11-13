# ESS AudioDrive and the BIOS DMA assignment

The report motivating SPEC.md §34.5.8 is an ESS1868F by BTC on an SBC
with AWARD 6.0: BIOS assigns DMA **0,3**, DMA 3 works, but selecting DMA 1
through ESSCFG or UniSound plays only 1–3 seconds. This is a user report;
we do not have that motherboard/card combination to reproduce it.

## ESSCFG source search (2026-11-13)

No public ESSCFG source was located. Searches covered the ESSCFG name,
`esscfg.c`, `esscfg.asm`, GitHub repositories/code and the surviving driver
archives. This is a search result, not proof that the source never existed.

A concrete copy of **ESSCFG.EXE** is in the hardware author's
[ES1868_ISA8 repository](https://github.com/hkzlab/ES1868_ISA8/tree/5bfcb01daa2482aa7d05869f0bc25d5926fe086f/software).
The author's [configuration instructions](https://github.com/hkzlab/ES1868_ISA8/blob/5bfcb01daa2482aa7d05869f0bc25d5926fe086f/README.md#configuration)
link the binary and recommend UniSound with `/XEA` for their card, including
on 8088/V20 machines. That switch recommendation is specific to that hardware;
it is not a documented fix for the BTC/AWARD report.

The downloaded ESSCFG.EXE has SHA-256
`5b33761b62c4f206963ea7833153b606fab57bb254da4fff185fccf34955289f`.
It was inspected as a binary reference, not copied into os8088 or used as a
build dependency. **We did not obtain its source and do not claim to have
reproduced its exact implementation.**

There is usable open source for the same hardware protocol:

- [Linux `sound/isa/es18xx.c`](https://github.com/torvalds/linux/blob/7e1392a42ea288d70a266f6553fe06f57f8647ea/sound/isa/es18xx.c),
  `snd_es18xx_identify`, `snd_es18xx_config_read/write` and
  `snd_es18xx_initialize`: E7h identity, four consecutive mixer-40h reads,
  the direct configuration index/data port, Audio 1 at 74h and Audio 2 at
  75h. The initialization distinguishes control-port parts from older ESS
  parts using DSP B1h/B2h. Linux is GPL-2.0-or-later; this change uses the
  register facts and independently written 8086 assembly, without importing
  Linux implementation code.
- ESS Technology's [ES1868 AudioDrive datasheet](https://linux.mathematik.tu-darmstadt.de/pub/linux/mirrors/misc/alsa/manuals/ess/DS1868B.PDF):
  printed pages 10–18 cover PnP, DMA-pin mapping and the two audio DMA engines;
  pages 34–35 cover mixer-40h identity, software reset and direct
  configuration ports. The standard PnP LDN tables and the direct controller
  interface are distinct: the older PnP DMA table even describes read-only
  values of 4. The Linux driver's direct-port accesses are the implementation
  evidence for programming 74h/75h. No bus-wide PnP LDN or isolation sequence
  is inferred from that table.

## What the report does and does not establish

The Sound Blaster compatibility engine is **Audio 1**. Audio 2 is another
DMA resource, not an SB16 16-bit DMA channel. Both ESS engines use the low
ISA DMA controller. A BIOS assigning 0 and 3 has not assigned 1.

Before this change os8088 always programmed host DMA 1, never identified
ESS and never configured the ESS route. That mismatch is a concrete defect
in our driver. An initial FIFO/buffer followed by stalled DMA or missing
interrupts could explain a short burst of sound, but the duration alone
cannot establish the cause. Host chipset routing, firmware reservations and
card-specific behavior remain possibilities. The same failure under two DOS
configurators is evidence against promising that another forced-DMA-1 write
will cure it.

The implementation keeps Audio 1 on 1 or 3 when already assigned there.
Otherwise it can disconnect Audio 2 and move its assigned 1 or 3 to Audio 1.
For 0,3 it selects **3**, reads the change back and programs every host DMA
operation and DOS's BLASTER value consistently. It avoids channel 0 (XT
refresh), channel 2 (floppy) and unassigned channels. A failed configuration
write restores the old routes and refuses PCM instead of guessing.

Refusal needs evidence. PCM is refused only when Audio 1 reads back on 0 or 2
with no 1 or 3 on Audio 2 to move, or when a route write does not read back.
Anything that cannot be confirmed - an unfinished identification, a
configuration port that is out of range or does not name the DSP's base,
routes reading 4, a DSP reset that does not answer - keeps DMA 1, which is
what every card got before this change. A card already on DMA 1 behind an
unreadable configuration port therefore still plays.

On an XT whose disk controller holds IRQ 5 (SPEC.md §34.5.6's test: a
`CPU_8086` machine with int 0Dh below E000h), DMA 3 is the disk's too - the
IBM/Xebec and ST11 controllers use both. The hkzlab card above is an 8-bit
ES1868 meant for such machines, so there 3 is never selected: Audio 1 on 3
moves to Audio 2's 1 if it has one, and is refused otherwise.

The moved routes are not restored at unload. A DSP reset leaves them and
`int 19h` re-runs no PnP setup, so the change lasts until a power-on or a
firmware/configurator pass; restoring them would keep three bytes resident.

Only positively identified ES1868/ES1869 parts receive configuration writes.
The audio I/O base and IRQ are preserved. Other logical devices, EEPROM and
DMA-pin mapping are untouched. Older ESS families retain the existing generic
SB path. PnP cards with no active DSP/configuration port still need firmware
or an external configurator; full ISA PnP enumeration is outside this change.
The driver cannot determine whether firmware silently reserved DMA 1 for
another device, and does not automatically switch a card already on DMA 1.

## Verification and field follow-up

`tests/essdma.py` (suite row `essdma`) executes the assembled shipping driver
under Unicorn with scripted ESS and 8237 ports, separate stack/data segments,
read-only configuration failures, timeouts and foreign-card controls, a slow
non-ESS DSP 3.01 reset, an XT disk on DMA 3, and `sbl_find` setting DMA 1
from zeroed cells. `--break-dma` deliberately restores the old DMA-1 address
port on DMA-3 cases and must fail; the row runs that arm itself. This validates the protocol and register discipline, not
physical audio, a real southbridge or undocumented card wiring.

The existing `sndfmt`, `sndfmtmarty`, `sndonce`, `sbirq5`, `picomem` and
`snddiag*` rows cover ordinary SB16/SB2 playback, recording-related DMA paths,
attach lifetime and diagnostics. Actual results are recorded in the PR.

On the reported BTC/AWARD machine, the useful field comparison is a cold
boot with BIOS resources 0,3, sustained playback and recording well beyond
three seconds, high-speed pause/resume and close/reopen, then DOS handoff
with `D3`. Compare a BIOS-configured 1 against 3 only if the motherboard can
reserve/route the corresponding channel. Read-back success is not evidence
that the motherboard actually delivers DMA. No physical ESS result is claimed
by the host tests or by a successful QEMU SB16 run.

Against `0129de488` the raw driver image is **10,517 → 10,919 bytes**
(+402). `snd_keep` moves 5,850 → 5,866 and `snd_keepmix` 8,679 → 8,695:
**+16 resident bytes**, including the three saved DMA bytes. Both retained
claims round to the same 512-byte granules as before (6KB without a mixer,
8.5KB with one). The full load-time claim crosses from 10.5KB to 11KB,
which changes the Drivers page's playing estimate from **26.5KB to 27KB**.
The [before/after captures](ess-sound/) are from actual MartyPC VGA, CGA and
Hercules guests, cropped to the Control Panel and enlarged 2× with nearest
neighbor sampling. They show that estimate, not ESS hardware playback.

Sound Debug's version-2 record reuses bits in its existing auto-init byte to
report the selected channel while keeping its 174-byte buffer size. Its DSP
row and BLASTER line now show DMA 3, and a refused route shows `none` without
a BLASTER line. The VGA/CGA/Hercules diagnostic screenshots use **reported
DMA 3 injected into the guest driver after the real DMA-1 tone has finished**.
They verify the displayed report; the emulator's card was not rerouted to 3.
The `snddiag*` rows also check the refused-route report. The shared symbol
reader now accepts `snddiag/sound.bin` and checks the map against that exact
debug binary before the test changes its reported state.
