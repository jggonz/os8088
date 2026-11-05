# MOUSECHK — Mouse Check, a mouse-connection diagnostic

**Status: in progress.** Contract: SPEC.md §126. Diary:
`docs/HOW-MOUSECHK-WAS-BUILT.md`. Built by `/story-fanout`; unchecked stories
below are not yet shipped behaviour.

## The ask (owner's words, 2026-11-05)

> Use moudiag as a guide to create a package that can help users debug their
> mouse connection. We're receiving reports that PS/2 mouse are not being
> detected in some machines. I want the app to open an interface and show all
> the auto-detected values for all the possible mouse that we support.
> Someone is trying to use a SBC like the one in the picture and can't get PS2
> mouse detected.

The board in the picture is an Advantech half-size ISA all-in-one SBC: an ST
**STPC** 486-class SoC, a **Phoenix BIOS "D486"** (1998), a **Winbond
W83977AF-A** super I/O whose keyboard controller carries AMI's MEGAKEY
firmware (`©AM.MEGA 87-96` on the chip), M-Systems DiskOnChip 2000, on-board
VGA, Ethernet and one mini-DIN at the bracket. Nothing in `vm/` emulates it.

## Why a package and not `MOUDIAG=1`

`MOUDIAG=1` (SPEC.md §9.4.6, §9.9.6) is a knob kernel: somebody has to build
it and send the disk. The reporter has a shipped disk. The shipped kernel
already keeps the boot verdict in the `'MO'` debug block (§9.4.2, §9.4.4),
but for PS/2 that is only the step the handshake reached (`mou_p2st`) and the
BIOS's command byte (`mou_p2cmd0`). The rest of MOUDIAG's PS/2 rows (`aux`,
`sub`, `pre`, `st`, `cm1`, `irq`) exist only in the knob build. So the
package does both:

1. it shows what the kernel decided at boot, for every pointer the system
   supports, read from the kernel's own record; and
2. on a button, it **runs the PS/2 handshake again itself**, step by step,
   recording every status and reply byte, including the alternative
   orderings §9.9.1 describes. That trace is what turns a field report into
   a kernel fix.

## Owner decisions (2026-11-05)

| # | decision | taken |
|---|---|---|
| O1 | How a shipped package reads the boot verdict, given §57.3 rule 1 | **A named exception**: §57.3 names MOUSECHK as the one shipped reader of `'MO'`, `'ID'` and `'VD'`. It checks the kernel against the build it was made with; on a mismatch it shows raw hex, not decoded meanings. **Zero resident kernel bytes.** Rejected: a new OSAPI slot (an ABI promise plus resident bytes); live probing only (the boot verdict would be lost) |
| O2 | May the package talk to the 8042 itself? | **Yes, on an explicit button** (*Probe PS/2*). Only when the kernel's PS/2 mouse is not live, with IRQ1 and IRQ12 masked, and the command byte and masks restored on every exit. Rejected: read-only |
| O3 | Where it ships | **`SYSTEM\` on the system disks** and on the live media, beside `SYSCHECK.O88`, so it is on the disk the machine booted from. Lead's reading: the 360KB system disk follows SPEC.md 24.3.1.7's 2026-10-22 rule for SYSCHECK and carries it on `util360.img` instead |

Standing constraints: **no kernel byte changes** (no file under `kernel/`,
`boot/` or `drivers/` is edited: zero resident RAM, no rung, no ABI
change). The package runs on kern_big, kern_emu and kern_mega. It works with
the keyboard mouse alone, because a user whose mouse is dead drives the
desktop with the arrows and keypad 5 (§9.6). It runs on 8088 to Pentium:
the PS/2 parts are gated on `OSAPI_CPU_INFO` exactly as §9.9.2 gates the
kernel's.

## Stories

Each story has an owner (the fan-out key), what "done" is, and how it is
verified. A story is ticked when it is SEEN working, not when it is built.

### Wave 1 — foundation (`found`)

- [x] **S1. The window and the row model.** `MOUSECHK.O88` opens *Mouse
  Check*: a scrolling list of sections, each a heading with a lamp (OK /
  Warning / Problem / Info / Not applicable) and label/value rows. Sections
  are built by `xx_rows` procs in their own `.inc` files through the shell's
  row API (§126.2). The shell redraws only the rows whose text changed.
  *Verify:* boot QEMU, launch from `A:\SYSTEM`, look on VGA and on CGA or
  Hercules.
- [x] **S2. Live refresh.** *SEEN*: the Summary's Position row followed the mouse and only that row redrew (QEMU kern_big, `other`'s look; kern_mega, the lead's). The list rebuilds every ~0.5 s (wake/timer, never
  a spin), so counters and the cursor position move while the user moves the
  mouse. *Verify:* move the mouse in QEMU and see the position row change
  and nothing else redrawn.
- [x] **S3. Controls and the report.** Buttons: *Refresh*, *Probe PS/2*
  (greyed with its reason, §47), *Save Report*, *Copy*, *Quit*. The keyboard
  reaches every one of them. *Save Report* writes `MOUSECHK.TXT`, and *Copy*
  puts the same text on the clipboard (§55). `OSAPI_ABOUT_SET` is answered.
  *Verify:* save on a writable B:, read the file back on the host.
- [x] **S4. Reading the kernel (O1).** `mc_dbgfind` finds a registry block,
  and the build check compares `'ID'`. `apps/mousechk/moblk.inc` holds the
  block offsets, and a fast-tier host test checks them against
  `kernel/mouse.inc`. The §57.3 exception is written. *Verify:* the
  fast-tier row is red when an offset is wrong.
- [x] **S5. Shipping (O3).** On every kern_big and kern_mega system disk at
  720KB, 1.44MB and 1.2MB, on util360, and on the live media. `t_livefull` is
  green and `docs/INDEX.md` is regenerated. *Verify:* `make`, plus
  `tools/os88disk.py --verify` listings.

### Wave 2 — the sections (fan-out)

- [x] **S6. Summary (`other`).** The top section: which pointer the kernel
  is using now (COM1 / COM2 / PS/2 / VMware absolute / USB / keyboard mouse
  only), the live x, y and buttons, the kernel kind and build, the CPU tier
  and the adapter. Ends with **one plain-language verdict line** for the
  user.
  *SEEN* on QEMU kern_big (COM1, PS/2) and kern_mega (PS/2); the no-mouse and mismatch verdicts are read from code only.

- [x] **S7. Serial mice (`serial`).** COM1 to COM4 from the BIOS data area;
  per probed UART, the kernel's boot record (identify burst: bytes, first
  byte, last tick, the "mouse-shaped" verdict), the contest (run, need, the
  winning row, the line it pulls, cross-wiring), the hot-plug poller's state,
  and live modem-status and line-status bits. It reads UART registers
  without side effects only, and never RBR.
  *SEEN* on QEMU: COM1, `MOUSEPORT=com2`, and `com2irq4`'s cross-wired verdict. LSR is not read (vserial: it would swallow the kernel's contest evidence).

- [x] **S8. PS/2, the boot record (`ps2`).** Tier gate; `mou_p2`/`mou_p2st`
  decoded into §9.9.1's steps in words; the device ID; `mou_p2cmd0` decoded
  bit by bit; the BIOS's own opinion (`int 11h` bit 2, the EBDA pointing-device
  flags); a live port-64h status read; packet state; and §9.9.6's decision
  table applied to what the shipped kernel can see.
  *SEEN* on QEMU: step 9/9 live on kern_big and kern_mega, step 0 on a `NOPS2` tree; every failure verdict is read from code only.

- [x] **S9. PS/2, the live probe (`ps2`).** *Probe PS/2* reruns the
  handshake with a full trace (each command, each status before and after,
  each reply byte or a timeout, in ticks). It runs the §9.9.1 sequence the
  kernel uses, then the alternatives: `0xA9` aux self-test, `0xA8` then
  re-read, the reset with and without `0xAD`, `0xD4 0xF2` get ID, and an
  opt-in BIOS `int 15h AX=C2xx` query. It leaves the controller as it found
  it. The verdict names the step that differs from the kernel's path. Gated:
  refused while the kernel's PS/2 mouse is live, refused on `CPU_8086`, and
  checked under kern_mega.
  *SEEN* on a QEMU `NOPS2` tree: the full trace, `Command byte 71>71: restored`, the BIOS leg, and the keyboard typing afterwards. QEMU's 8042 always answers, so the failure paths wait for the field machine.

- [x] **S10. VMware absolute and USB (`other`).** VMware backdoor: whether
  this kernel carries it (kern_emu), the `SYSTEM.CFG` bit, whether it won.
  USB: whether `USBMOUSE.DRV` is wanted and loaded (`OSAPI_DRV_CLASSK
  DRVC_POINT`), and which backend (CH375 or PicoMEM) answered, read with no
  writes to a chip the driver owns. Keyboard mouse: on or retired, and why.
  *SEEN* on QEMU kern_big and kern_mega. kern_emu shows raw hex (not shipped there, D5).

- [x] **S11. The gate (`gate`).** `tests/mousechk.py`, a QEMU row: launch,
  then assert the Summary's verdict and key rows for the default serial
  mouse, `MOUSEPORT=com2` and `MOUSEPORT=ps2`, plus the probe's trace on a
  `NOPS2=1` private build where the kernel never claimed the aux port. Also
  a mono look row. Registered in `tests/suite.py`, and not run by the agent.

### Wave 3 — integration (lead)

- [x] **S12.** Merge, size, one look per adapter, the deduplicated soak union
  once, `test-full` once, then the PR.

  *Done 2026-11-05*: `mousechkgate` passed all seven legs. The soak union
  (`syscheck`, `syscheckmono`, `wheelps2`, `p2restore`, `mouerrq`, `install`,
  `instkeep`) passed, and so did `test-full`, after `pins` and `guidelock-full`
  were paid (the report's claim registered, and the guide page written).

## Found while building (2026-11-05)

- [ ] **The BIOS leg's IRQ12** (owner call). On exactly the machines this is for, the kernel's failed probe
  left IRQ12 masked. A ROM whose `int 15h C2xx` waits on IRQ12 then fails every call, or waits without
  bound, so the leg is evidence only where the BIOS polls. The option is to open IRQ12 for the four calls
  when int 74h points into ROM. Kept masked, as O2 says.
- [ ] **kern_emu decoding.** Accepting kern_emu's 'ID' as a second fingerprint would decode the VMware
  section on the one kernel where it can be in use. It needs kern_emu built in `all`, a shipping choice.
- [ ] **USB "wanted but not loaded".** This needs SYSTEM.CFG read from the file (SYSCHECK's `sc_p_cfg`
  does it) and a buffer in the shell.
- [ ] **The field trace.** The Advantech SBC's saved `MOUSECHK.TXT` after *Probe PS/2* is the input to the
  kernel fix deferred below.

## Deferred

- **A kernel fix for the SBC.** It depends on the trace from the field
  machine (S9). Opened as a new item when a report arrives.
- **Publishing MOUDIAG's PS/2 scratch unconditionally** (about 10 bytes of
  `.bss`). This crosses the owner's zero-resident rule; S9 makes it
  unnecessary.
- **A Wire catalog entry.** It ships on the system disk; it can be added at a
  release.
