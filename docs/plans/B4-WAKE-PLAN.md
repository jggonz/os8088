# B4: drivers stay in the image, and SLEEP/WAKE re-arms them

> **STATUS (2026-11-11): BUILT, not yet run on the emulator rows or the
> iron.** The owner took tier 1 with the USB mouse's PicoMEM path, plus
> HTTPFS and VMMOUSE, in one batch; SOUND is deferred. SPEC.md §51.2.5 is
> the contract, §72.5.3 the lease, §87.6 steps 4b-6 the wake order. *What
> landed* below is the record; the costing under it is kept as written on
> `ba063985` except where *What landed* says the build overrode it, and its
> tier-3 verdict is rewritten on the owner's principle. Linked from
> docs/plans/HIBER-RESUME-PLAN.md §5.

## What landed

Six commits on `speed-and-fix` after `256b0cac`:

1. **The mechanism** - `DRVV_SLEEP` (5), `DRVV_WAKE` (6) and
   `OS88_DRV_WAKES` (bit 15 of header +10) in `drivers/os88drv.inc`, the
   mirrored `equ`s in `kernel/driver.inc`; `hbm_sweep`'s keep branch (DL = 0
   only) into the module's `[hbm_kept]`, `hbm_wakem` at both reload sites
   (`hbm_wake`, `hbm_reload`'s unwind), a refused WAKE `drv_unload_x` and
   joining the reload. ABI lock re-recorded, `DRV_VER` 14 unreleased, no
   bump. `t_drvverb` (fast): a declared driver must not refuse the pair, an
   undeclared one must, and the `WAKES` list pins who declares it.
2. **ETHER/PCIETHER** - as costed, with three changes the code asked for:
   the MAC is banked over `dhcp_off`/`dhcp_srv` (no new state); PCIETHER's
   wake find is `pce_find`, not a bare `pci_probe`, **because SLEEP's
   `rt_stop` gives the ring's claim back** - the prototype's WAKE would have
   started the bus master on freed memory; and a refused wake stops what
   `nic_init` started on the wrong card and leaves `[eth_up]` 0, so DETACH
   touches no port. The REQUEST is the INIT-REBOOT form (no option 54, +7
   bytes); `dhcp_wk` is cleared by any DISCOVER, and a NAK clears the whole
   16-byte address block.
3. **USBMOUSE** - the PicoMEM path (SLEEP `um_pmdetach`, WAKE
   `um_pmattach`, `[um_pm]` stepped down first, `[um_alive]` = 1 so a refused
   wake's DETACH touches nothing). **The CH375 path refuses SLEEP and is
   unloaded as before**: a park byte the worker honours, a second copy of
   the CHECK_EXIST echo (attach's code is overwritten by `um_buf`), the
   host-mode re-set and a re-enumeration - ESTIMATED 45-55 bytes and a worker
   parked mid-transaction - to save a 3-sector reload (~0.05-0.1 s on a hard
   disk).
4. **HTTPFS, VMMOUSE** - HTTPFS's WAKE **forgets its connection and KEEPS its
   index**, against the costing's "drop the index": `hf_fetch` starts from
   the root and the kernel moves inside a volume without an `FSV_CHDIR`, so a
   dropped index put the driver at the root under a window standing in a
   folder. VMMOUSE's WAKE is its attach again and **never refuses**: before
   B4 the sweep unloaded the row while `[vmm_on]` stayed 1 and the reload
   attached nothing, so a resumed QEMU's reset backdoor was never re-enabled;
   a refusal would reopen that window.
5. **The wake order** - the kept drivers are woken BEFORE `wm_paint_all`, and
   the busy pointer (`cur_busy_on`) goes up on the frame the paint finished,
   covering the deletes and every load still owed; `gfx_unlock` takes it off.
   Every kernel's `hbm_wake`, kern_mega's included.
6. **The rows** - `b4ether`, `b4ethswap`, `b4lease` (tests/b4wake.py, below).

**kern_mega keeps today's route** (`HBM_KEEPS` undefined, its sweep byte for
byte): the costing suggested that for drivers placed above 1MB, and every
driver this batch declares is either HIGHOK there (ETHER, PCIETHER: image and
socket pool in the pool, extra runs against `HBX_HMAX` = 62 on a hibernate
that fitted before) or worth nothing on that machine's park, which restores
nothing a wake would save. PCIETHER's ring is also a boot claim named by a
paragraph, which `rt_stop`'s free cannot find under protected mode - a kept
PCIETHER there would leak a ring per trip.

### Bytes, MEASURED against `256b0cac`

| | before | after | |
|---|---|---|---|
| kernel resident, kern_big / kern_small / kern_emu / kern_mega | | | **0** (`kernsize --json` identical; `kernel.bin` the same length on all four, the build number apart) |
| ETHER.DRV image + bss | 17,345 | 17,510 | **+165**; file 11,040 -> 11,152. **The claim crosses its rung, 34 -> 35 sectors** (`DRVM_IMG_ETH`): +512 bytes of heap while it is loaded. No cheap 102-byte trim was found; dropping `os88_ent`/`ep_bp` from the two verbs is ~12 |
| PCIETHER.DRV image + bss | 17,014 | 17,179 | +165, 34 sectors |
| USBMOUSE.DRV | 1,431 | 1,464 | +33, 3 sectors |
| HTTPFS.DRV image + bss | 5,539 | 5,553 | +14, 11 sectors |
| VMMOUSE.DRV (kern_emu) | 362 | 375 | +13, 1 sector |
| HIBER.DRV kern_big / kern_emu | 7,567 | 7,715 | +148 (keep branch, `hbm_wakem`, the clock), 22 sectors |
| HIBER.DRV kern_mega | 11,346 | 11,354 | +8 (the clock alone) |

### Seconds saved, PREDICTED

Per kept driver, on BOTH routes (the resume and the DOS return run the same
`hbm_wake`), against the reload it replaces: the file (`int 13h` calls at
~400 ms each off a floppy, a few tens of ms off a hard disk), its LZ4 decode
(~40 cycles a byte at 4.77 MHz), the probe, and READY.

| driver | hard disk | floppy-booted |
|---|---|---|
| ETHER / PCIETHER | **~0.3-0.5 s + the DHCP wait** (READY's `eth_dhcp_wait`: typically 0.2-2 s, up to `DHCP_WAIT` 6.0 s with no server - never measured on the iron) | ~1.0-1.3 s + the DHCP wait |
| USBMOUSE (PicoMEM) | ~0.05-0.1 s | ~0.4-0.8 s |
| HTTPFS | ~0.1-0.15 s (a 3.6KB file, ~0.03 s decode) | ~0.4-0.8 s |
| VMMOUSE (kern_emu, a VM) | negligible - and the pointer now works after a resume | |

On top of that, the stall moves: what is still read (SOUND, a CH375 mouse, a
refused wake, a suspend's rows) is read behind the busy pointer rather than
behind a desktop that looks ready.

### Rows

New (soak, QEMU - the closed list, entry 5; NOT run by the batch, the final
pass runs them; `secs` estimated, not measured): **`b4ether`** (kept,
woken, no DISCOVER, one INIT-REBOOT REQUEST; red arm `--red` clears the
header bit in the running driver), **`b4ethswap`** (another MAC: refused,
reloaded, a DISCOVER), **`b4lease`** (ACK / NAK / silence against the row's
own DHCP server, behind `-netdev socket`). **Not written**: `kdreturnnet` -
no QEMU harness drives the DOS handoff (`kdreturn` is MartyPC's, which has
no NIC), and the DOS return runs the very `hbm_wake` the three rows drive;
`b4usbm` - nothing in the tree models the PicoMEM's USB mouse. Existing rows
the change reaches: `hibernate`, `hibernatedrv`, `hibernatem`, `hibsumok`,
`hibsumbad`, `hmahiber`, `p2hiber`, `win16hiber`, `stikhiber`, `kdreturn`,
`kdreturnf`, `hmadosret`, `megahiber`, `megap2hiber`, `ethernet`,
`pciether`, `ethcfg`, `httpmount*`, `usbmouse`, `vmmouse`, and fast
`drvverb`, `drvmem`, `abilock`, `cswrite`.

## What the sweep takes today

`hbm_sweep` skips DISK, FILE, FILE2, CD, EMS (and VESA's row). So it unloads,
and the wake (and the DOS return, after `wm_paint_all`, input locked)
reloads: **SOUND** (row 0), **ETHER/PCIETHER** (DRVC_NET), **USBMOUSE**
(DRVC_POINT), **HTTPFS** (DRVC_HTTP), **VMMOUSE** (an OVL row, kern_emu).
**NET.DRV is DRVC_FILE and is already kept** — tier 2 owes it nothing.

## Mechanism (all tiers)

- **Declared opt-in**: bit 15 of header +10 (the FSV_SIZE word, "written and
  not read"; +15 is full). `OS88_DRV_WAKES` sets it. Verbs `DRVV_SLEEP` (5)
  and `DRVV_WAKE` (6) in os88drv.inc; mirrored `equ`s in driver.inc.
- **HIBER.DRV**: in `hbm_sweep`'s `.take`, when DL = 0 (the two hibernate
  callers; `hbm_drvsusp` always passes DRVC_NET, so OSAPI_DRV_SUSPEND is
  untouched), a flagged row is sent SLEEP and noted in a module word
  `hbm_kept` instead of `[hb_drvmask]`; a SLEEP refusal is today's unload.
  `hbm_reload_m` first spends `hbm_kept`: WAKE each; **CF=1 → `drv_unload_x`
  and the row joins DX**, so today's `hdl_load` re-probe is the fallback. The
  failure unwind (`hbm_reload`) gets it for free. Split so the WAKEs run
  **before** the DOS return's `wm_paint_all` (ms), the remaining reloads after.
- **ABI**: unreleased DRV_VER 14, so `abilock.py --update` without a bump
  (the prototype trips abilock exactly as predicted). An old driver has bit
  15 clear and is unloaded as today.

## Tier 1 — ETHER (and PCIETHER, same ethdrv.inc)

SLEEP: `eth_busy`=1 (held until WAKE), `eth_dropall` (RST every socket),
`nic_stop` — PCIETHER's bus-master ring must stop before the picture. WAKE:
bank the MAC, re-find at the same place (NE2000: `ne_probe` at `[eth_base]`;
RTL8139: `pci_probe`, the BAR may move after POST, no re-claim), `nic_init`,
**MAC compare → CF=1 on a different card**, ARP cache flushed, mutex freed,
then the **lease bypass**: if `DH_BOUND` and not manual, `dhcp_off`=`eth_ip`,
`dhcp_try`=0, `dhcp_wk`=1, `dhcp_request` — non-blocking. ACK keeps (clears
`dhcp_wk`); NAK zeroes `eth_ip` and goes to the existing DISCOVER;
`dhcp_timer` turns a wake REQUEST's DH_FAIL into DH_BOUND (silence keeps).
Note `dhcp_request` sends option 54 (the SELECTING form); RFC 2131's
INIT-REBOOT omits it (+~6 bytes for a flag) — most servers ACK either.

| | bytes |
|---|---|
| kernel resident, every kernel | **0** (equs; `DRVM_IMG_ETH` 34→35 is a constant) |
| ETHER.DRV image+bss | **17,345 → 17,509 (+164) MEASURED**; packed 11,040 → 11,146 (+106). All resident (no DRV_H_KEEP). **Crosses a granule**: 63 bytes of slack, so the claim goes 34→35 sectors |
| PCIETHER.DRV | **17,014 → 17,181 (+167) MEASURED**, packed +129, stays 34 sectors |
| HIBER.DRV | ESTIMATED **+110–140** (sweep branch ~58, wake loop ~62, `hbm_kept` 2): kern_big 7,565 → ~7,690, kern_mega 11,344 → ~11,470; transient (module) |
| kern_mega | 0 conventional; but a kept HIGHOK image (~17KB) and its HICLAIM socket pool (up to ~16KB) now ride the image's **extended section**, written and read each time (~+0.1–0.3 s PREDICTED on a 286 HDD) |
| kern_big image | 0 (the image is 0..`[mem_top]`) |

Time saved per resume and per DOS return (the same reload on both routes):
the ETHER reload is a 22-sector file (2–3 `int 13h` calls; ~1 s off a floppy),
an LZ4 decode of ~17KB (~0.15 s at 4.77 MHz at the boot decoder's 40
cycles/byte, §20.13), the probe (~20 ms), and **`eth_ready`'s blocking DHCP —
up to DHCP_WAIT = 6.0 s, never measured on the iron**.

| machine | resume | DOS return |
|---|---|---|
| (a) 5150 #1 (ST-225, no NIC) | **0** — no ETHER row | 0 |
| (b) PicoMEM 5150 #2 | **~0.5–2.5 s** typical; **~6.3 s** with no DHCP answer | same; it is the post-paint stall |
| (c) Victory 286 (kern_big, pmsys720) | **~0.4–2.2 s**; +~0.6–1 s more if booted off A: | same |

Risk: **moderate** — one polled driver, no IRQ, identity is a MAC. The new
failure is a server that ACKs nothing and silence keeping a stale address on a
moved machine (the NAK path covers a server that answers).

## Tier 2 — + SOUND, USBMOUSE

**USBMOUSE** (ESTIMATED **+40–55**, image 1,431, stays 3 sectors, all
resident): the PicoMEM path's SLEEP is `um_pmdetach` and WAKE `um_pmattach`
(detect, hook int 33h, enable) — both already resident; SLEEP **must** unhook
int 33h for the DOS handoff. The CH375 path needs a park byte the worker
honours and a re-enumerate (CHECK_EXIST echo, `um_state` reset).

**SOUND** (ESTIMATED **+110–170**, nearly all **below `snd_keep`, so resident
on every SOUND machine**; image 10,478, keep 5,828 / 8,644): SLEEP = the
DRVV_TIER off-leg (`sbl_detach`: DSP halt, worker out, vector off, claims
freed) plus the OPL key-off factored out of `snd_detach`; WAKE = `sbl_attach`
(reset at the settled base + the 12KB page-safe claim, resident because
DRVV_TIER uses it) + a DSP-version compare (identity), a minimal OPL re-init
(`opl_init` is attach-only, 185 B, given back), MPU UART re-entry, mixer
re-apply. **The PicoMEM switches its Sound Blaster OFF on every reset**
(picomem.inc), and `pm_init` (227 B) is given back after attach: a re-enable
from banked port/IRQ/DMA is ~40–60 B in PM_RES (resident on PicoMEM machines
only), or +227 to keep `pm_init` whole. HIBER.DRV: 0 more.

Time (incremental): (a) 0; (b) ~0.3–0.5 s (SOUND ~0.2–0.4: 9KB file,
~0.09 s decode, PM BIOS calls, IRQ discovery; USB ~0.05–0.1); (c) ~0.1–0.2 s
HD, +~1–1.5 s floppy-booted. Risk: **high** — a stream mid-play (handled as a
Control Panel tier switch mid-stream), the 8237 reset by POST (nothing kept:
the next open programs it), hiber-14's masks (SLEEP closes the line first),
and on the DOS return (no POST) a program that renegotiated the PicoMEM's IRQ.

## Tier 3 — the rest, and a generic mechanism

**Rewritten on the owner's principle** - *"We are not targeting my machines
only, we are building an OS"*: a driver is judged by its bytes and its risk,
never by whether the owner's three machines run it. The costing's first
verdict ("~0 s on the owner's machines, do not build") answered the wrong
question.

**HTTPFS and VMMOUSE: TAKEN, on bytes and risk.** HTTPFS owns no hardware,
so its wake is a few bytes with nothing to get wrong (the kept connection
forgotten - its socket died with the card's sleep - and the index kept, see
*What landed*); every machine with a Web drive mounted skips a reload and the
drive's zone is never withdrawn and re-added. VMMOUSE is kern_emu's, so every
VM user's: its wake is its own attach, 13 bytes, and it FIXES a resume that
came back with the pointer dead (the reload never re-attached the backdoor).

**A generic "re-attach in place": REFUSED, for reasons that hold on every
machine.** A default WAKE that re-runs the attach without re-reading the file
fails three ways whoever owns the machine: a driver that gives its attach
code back (`DRV_H_KEEP`, SOUND) has nothing to re-run - keeping it is
1.8-4.6 KB of heap all session; every attach assumes a freshly loaded image
(OS88_STATE zero, `DRVE_TWICE`) and the kernel does not know a driver's state
span to re-zero it; and an attach + READY re-runs ETHER's DHCP wait - the one
cost worth saving. A per-driver verb is an audit each driver passes on its
own terms, which is what this batch did.

**SOUND: DEFERRED, on resident bytes and risk.** Its wake is ~110-170 bytes
nearly all below `snd_keep` - resident on EVERY Sound Blaster machine, not
only the ones that hibernate - plus the PicoMEM's per-reset re-negotiation
(~40-60 B of `PM_RES`, or +227 to keep `pm_init`), and it is the
highest-risk wake in the tree: a stream mid-play, the 8237 reset by POST, a
DOS program that renegotiated the card's IRQ. It is worth its bytes only once
the field says the ~0.2-0.4 s SOUND reload is felt.

## Rows (as costed)

Existing: `hibernate`, `hibernatedrv`, `hibernatem`, `hibsumok`,
`hibsumbad`, `hmahiber`, `p2hiber`, `kdreturn`, `kdreturnf`, `kdreturnm`,
`megahiber`, `megahibfull`, `megap2hiber`, `ethernet`, `pciether`, `ethcfg`,
`netcoexist`, `netpromise`, `wire`, `httpmount`, `drvclaim`, `stikhiber`,
`win16hiber`; fast `drvverb`, `drvmem`, `abilock`; tier 2 adds `sound`,
`sndkeep`, `sndonce`, `sndmove`, `dossnd`, `picomem`, `usbmouse`, `emspico`.
New: **`b4ether`** (QEMU, ETHER is on the closed list: hibernate with a lease
and an open socket, resume; the row's segment unchanged, no DISCOVER, one
REQUEST for the same address; red arm: the header bit cleared → reloaded),
**`b4ethswap`** (another `-nic mac=` at resume → CF=1 → reload),
**`b4lease`** (silence keeps; NAK → DISCOVER, needs a host DHCP fixture
since slirp never NAKs), **`kdreturnnet`** (DOS return: WAKE before the
paint, no stall); tier 2 **`b4sound`** (MartyPC SB machine, a stream playing
at hibernate), **`b4pmsnd`** (`os8088_5150_herc_pm2_gla`: the SB re-enabled
after reset), **`b4usbm`**; and `t_drvverb` extended: a flagged driver must
not refuse SLEEP/WAKE.

## Recommendation (as costed; tier 3 superseded above)

1. **Tier 1: take it, with the USB mouse's PicoMEM path folded in** (it is
   only routing), when the DOS return's stall is fixed — it is that stall on
   both network machines. About +270 B shipped plus HIBER.DRV, 0 resident
   kernel bytes; ETHER's +164 costs a heap granule (+512 while loaded), so
   decide that byte count on its merits. Ask the field first for the one
   number the saving rests on: ETHER's DHCP time on the PicoMEM.
2. **Tier 2 (SOUND): defer** until tier 1 is on the iron and the field says
   the ~0.2–0.4 s SOUND reload is felt; ~150 resident bytes on every SOUND
   machine plus the PicoMEM's per-boot negotiation, at high risk.
3. ~~**Tier 3: do not build**, generic or per driver: ~0 s on the owner's
   machines.~~ Superseded: HTTPFS and VMMOUSE taken on bytes and risk, the
   generic mechanism refused on reasons every machine shares (*Tier 3*).
