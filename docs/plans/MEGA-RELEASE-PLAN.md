# kern_mega to a shippable state - the release audit (2026-11-07)

**Status: IN PROGRESS on branch `mega-release`** (cut from `elendilon` at
`09945b88`). The owner asked to "get kern_mega into a shippable state": comb
over it for refusals, for anything kern_big does that it cannot or does more
slowly, for the soak rows it lacks over critical paths, and drive every
program it supports as a user would on as many 86Box machines as can be
assembled. This file is that audit's record and the list of what remains.
docs/plans/KERN-MEGA-PLAN.md section 14 (14.145 onward) holds each fix's
detail, and its *Open before kern_mega goes live* list points here.

**The verdict so far.** kern_mega runs the whole shipped system. Every one
of the ~55 packages on its three disks and the 14 on-demand application
floppies was opened, used and closed on 86Box, on four adapters, three CPU
classes and the largest machine the bench can assemble. The defects it
found are below: twelve kern_mega regressions (eleven fixed here, one
under investigation) and sixteen defects kern_big ships too (seven fixed
here). **What still stands between kern_mega and a release is not code.** It
is the owner's release decision (with the release mechanics that follow
from it), one Victory 286 field session, the final scoped soak pass, and
the decisions listed under *For the owner*.

---

## 1. How it was done

| phase | what | where it is |
|---|---|---|
| static audit | five read-only passes: refusals and capability gaps (kernel, drivers); a package census (every package, on which disk, its kern_mega status, which rows reach it); the 1,111 soak rows with no `mega-` port triaged; performance against kern_big; every open item in the plans, FIELD-NOTES and the code | section 4, 5, 6 below |
| hands-on | five agents drove every program through `tools/os88ui.py` on 86Box (`OS88_MEGA=1 OS88_MEGA_EMU=86box`), screenshots read back, `faults` after each program, kern_big booted on the same machine wherever kern_mega misbehaved | section 2 |
| fixes | one worktree per batch, a row per fix seen RED on the tree before and green after | section 3 |
| ports | the critical unported soak rows tried on kern_mega, landed in `tests/megaport.txt` only when they pass and go red | section 6 |

**Machines** (86Box, docs/86BOX.md): the owner's mr286 (16 MHz 286, MR BIOS)
at 1MB, 4MB and 16MB, with VGA (OTI-067), EGA, CGA alone, Hercules alone and
VGA + Hercules; SB 2.0 and SB16; NE2000 on slirp; an IDE fixed disk; an ATAPI
CD-ROM; Lo-tech EMS; the Packard Bell 386SX with its PS/2 mouse; the
Micronics 386DX-25 with SB16 and CD-ROM; a Pentium 133 (16MB, SB16,
RTL8139, IDE C:, CD-ROM, Cirrus GD5446 VESA at 1024x768) - the "hardware
maximum", with SOUND, HDD, RAMDISK, CDROM and PCIETHER attached at once. The
486 profiles could not be booted (section 7, T1).

## 2. What works - the hands-on census

Every program below was launched, used for its purpose, closed, and its
memory seen to come back; "faults" means only the designed ones (the boot's
`mega_strcal` pair, the 286's reset path, write-through-CS completions).

- **The desktop**: every menu; folders, New Folder, Rename, Delete, Compress;
  copy both ways between drives (paste and drag); windows raised, dragged,
  zoomed, minimised to and restored from the dock; Format of a blank B: and
  Clone A: to B:; Sticky Apps; all eight Control Panel pages and their
  settings, themes (Color, Dark, Windows), dock positions, the RTC written
  at the panel's close surviving a Restart; the extended desktop on VGA +
  Hercules; six programs at once; Shut Down and Restart; INSTALL onto a
  blank 32MB C: (KERNEL.SYS byte-identical), a boot off C: with A: empty,
  hibernate to C: and resume with Notepad's text intact.
- **Productivity**: Notepad, TeXPad, Calc, Sheet (formula, SLK save and
  reopen), Chart, ArtfulType, FontView (pixel-identical to kern_big),
  Timer, SYSCHECK, Task Manager; from their own floppies Word (1.44MB and
  1.2MB), CWORD (overlay commands, Save As), Scribe (undo as fast as kern_big).
- **Media**: Audio, Tracker (all adapters, VESA, the speaker; the SB capture
  matches kern_big's block for block), MIDIRack (OPL2 and speaker), Piano,
  Mixer, XTMP3 (live on the Pentium, and after 14.150 on the 386DX-25),
  WaveRack (after 14.149), CDRack, the Video Player, PiXEL, Fractal, Bounce,
  REDLINE, Paint's colour face; every full-screen bracket back to the desktop
  with 0 pixels different; VESA.DRV at 1024x768 on the Pentium's Cirrus card.
- **Games** (VGA, EGA, Hercules, CGA; the mouse games on PS/2 too): Mines,
  Solitaire, Arkanoid, TameGram, WireBall, WireFleet, Tank, Cyclone, Skies,
  Dot Delirium, Missile, PIXELSTEIN 3D (its timedemo identical to kern_big's:
  VGA 59.0 fps, CGA 77.6, Hercules 81.9), Gorillas, DrMarco, isoTron,
  RealDMG (after 14.147), ARCADE's ROM-less cabinet; from their floppies
  PacCman, 1942 (its kern_mega code ran for the first time), Excitebike,
  Video Battle, Lemmings (after 14.148), RunCPM (MBASIC), C64, Apple II,
  WEAVE and LOOM (a bundle packed on kern_mega byte-identical to the shipped
  one).
- **Network and DOS**: the Browser over ETHER.DRV (NE2000) and PCIETHER.DRV
  (RTL8139), Telnet, FTPD driven by Python's ftplib, WireChat, The Wire's
  catalogue and Add to Disk, HTTPFS/HTTPPAGE (a Web Drive D: launching CALC);
  the DOS prompt (VER, DIR, TYPE, COPY), a .COM, an MZ .EXE and OS88CZ.COM
  on the trip, Load DOS in the HMA on and off, a DOS program's Sound Blaster
  and INT 33h mouse; WIN16 with the tree's TINY16; EMS.DRV, SERIAL.DRV
  (WireFleet on COM2 through a loopback plug), the RAM disk, HDD.DRV,
  CDROM.DRV.
- **Memory**: 1MB (kern_mega's floor; nothing refused, four programs fill it),
  4MB, 16MB (15.5M reported).

## 3. Fixed on this branch

| | defect | kernels | fix | row (red before, green after) |
|---|---|---|---|---|
| 14.145 | Ctrl + grey keys / keypad stored the plain key (Lemmings' fine scroll, every Ctrl+arrow a Windows or Telnet program reads); Pause toggled Num Lock; Ctrl+Break toggled Scroll Lock | mega | `mega_k09`'s ctrl table, the ROM's E1 flag and pause bit | `megakbcodes` |
| 14.146 | Ctrl+Alt + the grey Del did nothing | mega | the grey arm asks the warm-boot question first | `megakbcodes` (last leg) |
| 14.147 | RealDMG refused the SHIPPED packed GBDEMO.GB | mega (and kern_big for carts past one claim) | a packed cartridge takes the one claim that unpacks | `mega-gbship` |
| 14.148 | Lemmings at ~60% speed: ~3,000 write-through-CS traps a second | mega | no write through CS in the blitters (43,410 traps a play -> 0) | `cswrite` walks `apps/lemmings/` |
| 14.149 | WaveRack closed with a #GP on a 16-bit clip (paragraph arithmetic on a selector) | mega | the hold through a bound window | `waverackmega` |
| 14.150 | XTMP3 could not play live on a 386DX-25 (~3,470 traps); ~80-100 traps a speaker play; ~990 a Shut Down screen; Notepad's save; ~1,700 an install | mega | an XT-part arm with a data selector; DS stores in the speaker setup, the spinner, Notepad and the installer | `x3live386`, `x3xtunitmega`, `cswrite` |
| 14.152 | **The Wire's Load Program crashed the machine on any packed .O88** - every release package | both | the image arm expands a packed image through the disk path's step 6a (+4 bytes `.cold`) | `pkgrun` G, `mega-pkgrun`, `thewire` 19 |
| 14.156 | A 22 kHz Sound Blaster stream from a floppy had a 0.45-0.54 s silence every 2.1 s (3.57 s in a 10 s clip) | mega | floppy DMA in place below 16MB (`dsk_runcap .hi`, +18 bytes `.cold`), and `mega_fdgo` skips the seek and settle while the cylinder is unchanged (+32 bytes `.mega`) | `megasbgap` (longest 290-300 ms; kern_big 100) |
| 14.157 | Shut Down left the scheduler ticking under the BIOS clock (IDT gate 20h still `sch_isr`) and a selector in IVT 08h | mega | CTRL.DRV puts `mega_t08` in the gate and the BIOS's tick back in the IVT | `mega-shutdown` |
| 14.158 | A resume refused by the streaming stub (a damaged image) never took its restart key, and once it did, asked the same question again | mega | the stub switches the 8042's AUX off, drains it, unmasks IRQ1, and leaves `'MRF1'` in the BDA mailbox for `hbm_ask` to drop the pair | `mega-hibsumbad` |
| - | Paint's BMP save wrote a corrupt file on VGA/EGA (planar canvas written raw) - data loss | both | packed in place for the write | `paintbmpsave` |
| - | DOS COPY of >~1KB to a FAT16 C: failed; `DIR B:` printed A:'s free bytes; `B:NAME.COM` lost a letter | both | the copy sized on the destination; footer before `dsh_home`; one `dec di` | `doscopyhd`, `dosdirsw`, `dosext` |
| - | FTPD stalled on LIST/NLST of a missing folder | both | an empty listing answered 550 | `ftpd` 8f |
| - | Chart showed uninitialised memory until a file was opened | both | the canvas cleared | `chartread` 0 |

Budgets after all of it: kern_big `KERN_SIZE` 96,768 of 100,864 (unchanged),
kern_mega `MEGA_RESIDENT` 62,976 of 68,096 (unchanged); kern_small and the
kern_big image byte-identical for every kern_mega-only change.

**Written and not committed** (no row that can be proven red on the bench):
CDRack's status line kept the previous track after an auto-advance, and the
Mixer told an SB 2.0 user to "Load SOUND in Drivers" with SOUND.DRV loaded.
The patch is kept for whoever writes the 86Box rows.

## 4. Open defects

### 4.1 kern_mega regressions still open

- **M6's last 300 ms** - 14.156 fixed the gap (section 3); what is left is
  the ring's first pass, where the pool claim crosses a 64KB physical page
  and one run loses a revolution. A choice for the owner (section 8, item 8).
- **Write-through-CS cost, the rest of the census** (14.150's `--census`):
  `apps/` holds 1,015 writes through CS in 97 files - Sheet 305 (164 a macro
  run), WEAVE's VM 152 (~10,000 a second in PONG.WAB, no visible slowdown on
  86Box), WIN16 ~250, the C64 JIT 59, DOS.O88 28, RealDMG 12 (~94 a cartridge
  load). Each costs ~884 clocks on a real 286. None blocks a release; a
  package-by-package pass, measured, is the way through.
- **Stale-selector #GPs at `api_x.done` and `kret_es`** after a claim is
  freed (isoTron and RealDMG closes, Task Manager, Notepad's save): completed
  by `mega_gpfix .stale`, harmless, and the `MXT_ESPOP` case - but docs/86BOX.md
  names only `mega_ldt_free.out` as an expected site. Document them.

### 4.2 Defects kern_big ships too (not regressions; a release still ships them)

| defect | where | weight |
|---|---|---|
| a "Save changes?" alert can be buried under its own window, which keeps taking keys; the close never completes | the window manager's modal ordering (h1 #4) | should |
| WaveRack's first wake does up to 8 disk reads before the playhead or Stop is served (~3 s on both kernels) | SPEC.md 123.6; proposal: one read per wake | owner |
| WIN16: a floating-point program on an FPU-less 286 closes with #UD on kern_mega and hangs kern_big's desktop; WIN16.TXT promises an 8087 emulator | apps/win16 | should |
| Windows theme: a Disk window opened in one sequence has no frame or title | h2 N1 | should |
| extended desktop: Tracker full screen on the Hercules leaves the VGA showing the desktop (SPEC 39.18 says blank) | h2 N2 | nice |
| Telnet: ANSI bright yellow shows cyan on VGA | `con_pen` | nice |
| Task Manager's MEM column cuts the K off "259.5" | Task Manager | nice |
| Word's F12 saved an untitled document straight to DOCUMENT.DOC with no dialog (kern_big not checked) | apps/cword or word | check |
| a drive with no backslash resolves to that drive's ROOT, not its current directory | SPEC 96.33.21 | recorded |
| CDRack status line, the Mixer's SB 2.0 message | patch in hand (section 3) | nice |

### 4.3 From the static audit

- **Old binaries** (SPEC.md 41.16): a package built before MC7 that inlined
  `OS88_CODESEL`/`op_cseg` (C packages with overlays, every PARTS package,
  PIXELSTEIN 3D, LOOM, WEAVE) far-calls an unrelated LDT entry on kern_mega
  with no fault. Harmless while nobody has kern_mega; after a release, any
  older apps disk in a kern_mega machine meets it. **A decision** (section 8).
- **A refused switch to protected mode** ("No PM: ...") leaves kern_mega in
  real mode with no extended memory at all - its disks carry no XMEM.DRV -
  which is less than kern_big on the same machine. Untested. Document, and a
  row (Bochs can refuse the switch).
- HTTPFS.DRV, USBMOUSE.DRV and VESA.DRV always load in conventional memory
  (not marked for above 1MB); VGA-only code stays resident on mono machines
  (its slide needs ROM adoption); the read-ahead fill hint is compiled out.
  Each costs conventional memory or speed, none correctness.
- **Performance never measured**: boot, first mount, app launch, file open
  and save, a window drag, a menu, a redraw - kern_mega against kern_big on
  the same machine. 86Box gives ratios (with the tree's patch 03), the
  Victory absolute times. Predicted costs worth checking first: every HD
  floppy mount's first read guesses the data rate (`mega_fd13`, ~+0.4 s a
  mount, invisible to Bochs); RAMDISK.DRV and NET.DRV lack `FSCAP_SEL`, so a
  read into a buffer above 1MB is one driver call per 512 bytes (one network
  request each on NET.DRV); `mega_hibnc` bounces a BIOS-only fixed disk 9
  sectors a reset; every VESA bank switch on a real VBE card is a reset.

## 5. Refusals

**The owner's decided refusals all stand** (confirmed in the code): ROM
adoption, XMEM.DRV and COLDHMA.SYS, the windowed DOS (refused gracefully -
programs run full screen and their output appears in the window after,
though nothing tells the user why), HDD.DRV's fill straight into XMS, and
the shipped Video Player and RealDMG (their arms run instead).

**Refusals not on that list, all deliberate**: kern_big's DOS.O88 ("Use this
disk's DOS.O88"); hibernate on a 512-byte-cluster volume in protected mode (a
foreign-formatted C: only), an image resumes only in the mode it was taken
in, and hibernate is greyed with no extended pool; a sound DMA ring in a claim
above 1MB; a package's own software interrupts only for 21h, 2Fh, 31h,
34h-3Dh (SPEC 20.18.1); the speaker/Covox tier of the SHIPPED Audio,
Tracker, MIDIRack and XTMP3 (their arms play it); WIN16's shipped build
(its arm is on `megawin16.img` - but its message, *"WIN16 needs real mode"*,
is stale and should point at that disk); DOS programs have no network (no
windowed DOS).

## 6. Test coverage

**Before this branch**: ~328 `mega-` ports and ~60 native kern_mega rows; a
2,006-row whole-tier soak of elendilon ran green on 2026-11-07 apart from
one row (8c467e3d). **Gaps found**: 28 critical soak rows kern_mega had
never run (close-and-ask, shut down, hibernate with a damaged image,
compaction under a live Sheet/Paint/RAM disk, a floppy swapped under a
window, DMA staging, held write streams cut/failing/full, FAT16 defrag, the
FCB write family, FTPD end to end, the packet driver, WaveRack, the
keyboard ISR's two rows, task-stack depth, the heap's door, 16-bit DMA,
four DOS-comes-home-changed rows, WIN16 file I/O), and for most games
`megasweep`'s five-second open is the only kern_mega coverage.

**New rows on this branch**: `megakbcodes`, `mega-gbship`, `waverackmega`,
`x3live386`, `x3xtunitmega`, `mega-pkgrun`, `paintbmpsave`, `doscopyhd`;
extended: `cswrite`, `pkgrun`, `thewire`, `dosdirsw`, `dosext`, `ftpd`,
`chartread`.

**Ports of the 28** (6.1).

### 6.1 The soak ports (`mega-soakport`, merged)

**Landed in `tests/megaport.txt`**, each passed through `os88test.py soak -k
mega-<row>` and seen RED on a kern_mega with one source line broken (Bochs
has no POKE): `dispclose`, `rdmove` and `sheetmove` (on a 1MB machine; red on
a `HEAPCOMPACT=0` kern_mega), `fdswap` (red with the swap detector served
from the read-ahead; the base row's own break stays green here, the cache's
chunks being keyed by disk signature), `wseqfull`, `wseqcut`, `wseqioerr`,
`dosfcbwr`, `stk0water` (a MEASUREMENT: 276 of 512 bytes, 238 on kern_big),
`hibsumok`, `fsckhd`; and, with their defects fixed on this branch,
`shutdown` (14.157) and `hibsumbad` (14.158).

**Harness fixed on the way**: `os88megabochs._video` reports text, so a wait
for a text screen ends under Bochs; `os88qemu.Private` goes through the
kern_mega server rather than starting kern_big; `ftpd.py`/`ftpkeep.py`'s
`except (OSError, ftplib.all_errors)` was a TypeError on the first refused
connect; `viddisk.py`, `hibernate.py` and `fsckhd.py` build C: from
`megak/`'s kernel files (they put kern_big's there, which Bochs booted).

**Not landed**, each for a reason:

| row | why |
|---|---|
| `paintmove` | checks 1-4 pass; 5 fails because heapfrag draws text below its own window onto Paint and kern_mega's raise does not erase it - kern_big under MartyPC VGA passes the same check. **Unexplained; a candidate defect** - *(2026-11-07: settled, NOT a kernel defect, and landed - item 9)* |
| `memp` | 8 of 11: memp.asm checks alignment on what is a selector here (check 7 passes by accident). The test, not the kernel |
| `ftpd` | Bochs's slirp takes 5 forward rules, the row needs 9, and the launcher drops the host forwards (T2) |
| `hibernatem` | the 86Box backend reports a Hercules as graphics, so the text-mode wait never ends (T6) |
| `dospkt` | N/A: the DOS trip greys Network on kern_mega - no packet driver |
| `kdreturngfx`, `kdreturnpit` | N/A as ports (kern_big's hibernate handoff); a scratch probe of the trip came home clean, exit code 042, ticks at 18.2 Hz |
| `kbd4f`, `kbdpreempt`, `sndfmt`, `dskwstage`, `kdreturnm`, `kdreturnmode` | not attempted, harness reasons in the agent's report |

**Still uncovered after this branch**: a 1bpp-adapter kern_mega row (Bochs
is VGA only; the 86Box port reaches CGA and Hercules and no row asks it);
PicoMEM under kern_mega in this container (`tools/picomem/build.sh` has to
be run first); NET.DRV's parallel/serial link (two machines); ARCADE's
`arcmega`/`arcmega286` (ARCADE-MEGA-PLAN M4/M5); `vesablitd` on kern_mega;
the `win16mega*` rows (never run); serial play in WireBall/WireFleet (no
peer).

## 7. Test infrastructure found wanting

- **T1** the kern_mega 86Box port boots only profiles with an `nvr-seed/`:
  the mr286, and (14.150) `vm/386-mega-cdrom`. `386dx`, `486`,
  `486-vesa-desk` and `pentium` stop in BIOS setup; the two 486s could not
  be brought past it at all (AMI setup ignores injected arrows; after a save
  POST hangs at "WAIT......"). Seed the 386dx and Pentium the way 14.150
  seeded the 386; the 486 needs a CMOS made by hand in 86Box's UI.
- **T2** under kern_mega `at386_vga_ps2` still gives the mr286 with a serial
  mouse (`_ps2` is honoured only off kern_mega, tools/os88box86api.py
  ~299-311); `machine_config` always overrides a profile's card from the
  machine name; `_ne2k` overwrites A:'s SYSTEM.CFG; no knob for a slirp port
  forward (FTPD needed one); the Pentium's Award "press F1" and MR BIOS's
  warm-boot "Parallel Port Configuration Change" prompts are not in
  `BIOS_PROMPTS`.
- **T3** `cmos_primed` zeroes byte 0Fh of any seed file over 128 bytes,
  which corrupts a flash image (the Pentium's `p54tp4xe.bin`).
- **T4** a freed `mem_tab` record keeps `MC_PARA` (only `MC_SEG` is zeroed),
  which reads as a leak to a test that keys on it.
- **T5** `os88ui`'s typing is unpaced: keys typed within ~1 s of a program
  opening are dropped on both kernels.
- **T6** the 86Box backend reports a Hercules in a text mode as graphics, so
  a row that waits for a text screen (`hibernatem`) never ends there.
- **T8** (kern_big's, not kern_mega's) ~40 rows capture with
  `m.vram("cga")`, which on a MartyPC VGA machine reads an unmapped B8000
  and compares two blank frames - `paintmove` passed that way on
  `os8088_xt_vga` (docs/TEST-INCIDENTS.md 81). Harmless on each row's own CGA
  machine; a row run with `--machine` on a VGA one wants `paintmove.py`'s
  `glass()`. The kern_mega ports are NOT affected: the Bochs shim's `vram()`
  ignores the card and reads the glass.
- **T7** Bochs's slirp holds 5 port forwards and the kern_mega launcher drops
  the host's, so FTPD end to end cannot run on kern_mega (QEMU-only rows).

## 8. For the owner

1. **The release decision itself** (KERN-MEGA-PLAN 14.126: *"not 'released'
   yet - so we're not building disks in the release zip/skill"*). If yes:
   the mechanics in section 9.
2. **Old binaries on kern_mega** (4.3): a loader refusal for pre-MC7
   packages (a header flag the post-MC7 SDK sets - an ABI note under
   SPEC.md 20.2.0.1) or accept it and say *"use only this release's disks
   with the 286 system"* in the notes and the guide.
3. **The Victory session** (section 10) - one visit, one list.
4. **WaveRack's first wake** (4.2): one read per wake, so the playhead and
   Stop are served between reads on both kernels - a visible change.
5. **WaveRack `OS88_HIGH`**: its exemption's reason is gone; declaring it
   wants `test-full`'s gate.
6. **Word on kern_mega**: WORD.O88 ships only on `office360.img`, and kern_mega
   has no 360KB disks, so no kern_mega disk carries Word (it runs from
   `word.img`). Likewise the Video Player is not on kern_mega's system disk,
   XTMP3 is not on `megaapps120.img`, and `nettree/mega` has no WIN16 (its
   Makefile comment, "it refuses there", is out of date).
7. **The 1,015 writes through CS left in `apps/`**: a speed pass per
   package, measured, or release notes.

8. **M6's last 300 ms** (KERN-MEGA-PLAN 14.156): (a) AUDIO claims its
   look-ahead through `OSAPI_MEM_CLAIM_DMA` on every kernel - it IS a DMA
   target - which on kern_mega moves 32KB from the pool into conventional
   memory; or (b) a page-safe pool door on kern_mega, so DMA claims go high
   too. Measured with (a) as an experiment: longest 150 ms, kern_big's figure.
9. **`paintmove` check 5 on kern_mega** (6.1): text heapfrag draws below its
   window is left on Paint after a raise. Worth one look before a release.
   *(2026-11-07: settled - the FIXTURE, on both kernels, and the port
   landed.)* The text is HEAPFRAG's: once its posted pass has recorded
   checks 17 and 18 a repaint lays out 18 rows and six figure lines, and
   the last line starts at y=284, below a frame that ends at 276. The raise
   that the row uses to reach heapfrag's close box runs W_PAINT through
   `wm_front_b` with no region armed (SPEC.md 11.3.3 arms one only in
   `wm_dmg_wins`), so the line lands on Paint, and heapfrag's close repairs
   its own frame and nothing below it. kern_big under MartyPC's VGA draws
   the SAME pixels (traced: `font_run_x` at CX=154 DX=284, `wm_clip_n` 0,
   on both) and fails the same check at the same x 154..191, y 284..290;
   "kern_big passes" was the row reading `m.vram("cga")` on a VGA machine,
   0xB8000 blank in both captures. Fixed in the fixture (heapfrag's paint
   stops at its content box, figures on one line) and in the row (`glass`,
   the card asked rather than named).

## 9. Release mechanics (if the owner says yes)

- `make mega` already builds six images; `make arcrelmega` adds two and is
  not part of it.
- The images are in no release artefact: not in `$(SHIPIMGS)`, not in mkzip's
  ITEMS. Adding them to `$(SHIPIMGS)` needs boot-signature slots (SPEC
  18.8.2.2) and puts `make mega` into `all`, as kern_small's disks are; the
  image gates (`t_image`, `t_diskverify`, `t_canary`, `t_blobruns`) each hold
  their own list (`t_canary` skips megak on purpose today).
- mkzip: a new group with 8.3 names, and a second freshness reference
  (`build/megak/kernel.sys`).
- User documentation: none exists. A guide disk page and a README section
  (which machines, what to expect, which disks pair with which); several
  manuals already describe "the protected-mode build for the 286", which a
  user cannot get. Whether the live USB/CD gets a kern_mega option is
  SPEC.md 80's first.
- `budget-check` already measures kern_mega; the release skill's prose says
  three kernels and should say four.
- `test-full` never boots kern_mega, so a first release owes the scoped
  `mega-*` and `box-*` union once in its final pass.
- Fork rule 3's disk list (CLAUDE.md) should name the kern_mega disks.

## 10. Field runs owed (the Victory 286, one session)

- **14.125's empty-drive freeze** - the only open defect reported off
  kern_mega iron, "FIX ON TEST": an install, drive switching, the File
  Manager on an empty drive, an empty drive right after a Video Player play;
  `make megadiag`'s disk if it freezes. IR15 (77h) has the same exposure and
  is not covered.
- **14.156's seek skip** touches 14.125's native floppy path: `mega_fdgo`
  now skips the seek and head settle while 40:94+unit says the cylinder is
  unchanged. A 22 kHz SB play off a floppy, an install, and a disk swap
  between two reads of the same cylinder (which the media-change line, not
  the cylinder, must catch).
- **14.158's resume refusal**: a damaged image, the key, the next boot
  ordinary - and a PS/2 mouse working on it (the stub's A7h is undone by
  `mou_p2_init`'s A8h; a serial mouse is untouched).
- VESA on a real VBE card, above 800x600.
- The PicoMEM: its USB mouse through a boot, a play and a DOS trip; its IRQ
  reflections; T69/LASTX.
- KERN-MEGA-DOS-PLAN's untested-on-iron list (`mega_dprime` is commented
  "UNVERIFIED"); 14.64's lost-edge watchdog (wiggle the mouse through each
  DOS resume); the 640K-1MB Task Manager rows.
- kern_mega has run on ONE physical machine; the release notes should name
  it, and a second 286 (the Compaq Portable III) or any 386 would widen that.

## 11. The final pass

**Run, 2026-11-07, at 1a37e25e** - the union below in ONE scoped call (87
rows, kern_big's and kern_mega's, `build/soak/20261107-204757`): **87 passed,
0 failed, 0 skipped** in 28 minutes. Then `make test-full` once: 87 passed, 1
failed, 2 skipped (`w16cens`, `w16censall`: the WIN16 fixtures, not on this
box). The failure was `guidelock-full`, RealDMG's page owing 14.147's new
refusal; paid in bcf768f1 and the row re-run alone, green. Nothing in it moved
a kernel or a package.

Run ONCE after the last batch, in one call (CLAUDE.md, Testing):

```
python3 tools/os88test.py soak \
  -k megakbcodes -k mega-telansi -k megaaltrdg -k megakbtrip \
  -k mega-gbship -k gbship -k gbbank -k gbdemo -k rdgmega -k rdgaotmega \
  -k mega-gbsave -k mega-gbkeys -k mega-gbstate \
  -k lemmings -k lemmega -k lemmove \
  -k waverackmega -k waverack -k waverackplay -k waverackdisk -k waverackrec -k waverackedit \
  -k x3live386 -k x3xtunitmega -k x3xtunit -k x3mega -k xtmp3live -k xtmp3eng -k xtmp3xt \
  -k spkfx -k spkbench -k apspk -k apspkcovox -k trkspk -k trkspkcovox -k trkspklevel \
  -k midirackspk -k midirackcovoxpoll -k midirackwt -k vidspk -k vidspkcounts -k vidspkcovox -k vidspkshape \
  -k shutdown -k install -k instdeep -k megainst -k npscroll \
  -k pkgrun -k mega-pkgrun -k thewire -k paintbmpsave \
  -k doscopyhd -k dosdirsw -k mega-dosdirsw -k dosext -k dosshell -k dostype -k mega-dostype -k doscom -k mega-doscom \
  -k ftpd -k chartread
```

and the kern_mega rows this branch added or ported last:

```
python3 tools/os88test.py soak \
  -k megasbgap -k mega-fcpcopy -k mega-diskclone -k mega-imgmount -k mega-imgswap -k mega-fdlgchoose \
  -k mega-dispclose -k mega-rdmove -k mega-sheetmove -k mega-fdswap \
  -k mega-wseqfull -k mega-wseqcut -k mega-wseqioerr -k mega-dosfcbwr -k mega-stk0water \
  -k mega-hibsumok -k mega-hibsumbad -k mega-stikhiber -k megahiber -k megahibfull \
  -k mega-fsckhd -k mega-shutdown
```

plus the eleven kern_big base rows the soak port's harness edits touched
(`dispclose`, `rdmove`, `sheetmove`, `fdswap`, `wseqfull`, `wseqcut`,
`wseqioerr`, `hibsumok`, `hibsumbad`, `fsckhd`, `shutdown`; they passed
11/11 on MartyPC in the port's own run), then `make test-full` once (14.152
moved every kernel by 4 bytes).
