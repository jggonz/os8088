# Bochs: running kern_mega under an emulator that checks protected mode

Bochs is the instrument for **kern_mega** (docs/plans/KERN-MEGA-PLAN.md), and
for nothing else in this tree. Every other kernel runs in real mode, where
MartyPC is the default instrument and QEMU the fallback (docs/TESTING.md).
This file is how to install it, drive it, read it, and where it lies.

## Why Bochs and not QEMU

QEMU's TCG checks no segment limit on a data access and lets code write
through CS. So on QEMU kern_mega runs code that faults on a real 286: every
one of the Victory 286's first five faults ran clean there. Bochs checks every
access the way the CPU does, and reproduced those faults to the CS:IP and SP
in the field photographs. If kern_mega faults under Bochs it will fault on the
iron. If it runs under Bochs it will probably run on the iron, except for
the five blind spots at the end of this file.

## Install (Ubuntu container, as root)

```
apt-get install -y bochs bochsbios vgabios bochs-x xvfb xdotool x11-apps imagemagick
```

That is the whole of it: Debian's Bochs 2.7 has the debugger compiled in,
which the harness relies on. **The BIOS must be `BIOS-bochs-legacy`**. The
default BIOS's 32-bit init crashes here, and the harness's bochsrc already
names the legacy one. Nothing needs building, and no ROM is fetched.

## The machine the harness builds

`tools/os88bochs.py` writes `OUT/bochsrc` (default OUT is `build/bochs`) and
starts Bochs on an Xvfb display - `:99`, or `$OS88_BOCHS_DISPLAY` - that it
starts if that display is not up:

| part | what |
|---|---|
| CPU | Bochs's default, a 386-class CPU - see blind spot 2 |
| RAM | 16MB (`megs: 16`), so the XMEM pool and `.cold` above 1MB are real |
| video | Bochs VGA with VBE, the stock `vgabios.bin` |
| A: | `build/mega.img`, COPIED to `OUT/a.img` each boot (1.44MB) |
| B: | `build/apps120.img` by default, or `--b IMG`; `--btype 1_2` or `1_44` is the DRIVE, the media size is read off the image |
| C: | a 20MB ATA disk (615/4/17) - see `--c` below |
| mouse | PS/2, captured with ctrl + middle button (`toggle=ctrl+mbutton`) |
| E9 | `port_e9_hack`: every byte the kernel writes to port E9h lands in `OUT/e9.out` |
| sound | `--sb`: an SB16 at 220h, IRQ 5, DMA 1, its output written to `OUT/sb.wav` |
| network | `--ne2k`: an NE2000 at 300h, IRQ 10, on Bochs's own slirp (the host is 10.0.2.2). `$OS88_BOCHS_SLIRP` names a slirp config for it - a first line `# slirp config`, then up to five `hostfwd = tcp::2121-:21` lines - which is how a HOST client reaches a server in the guest: FTPD's control port and four of its passive ports, with `curl --ftp-skip-pasv-ip` (KERN-MEGA-PLAN 14.46) |
| clock | realtime |

## Commands

```
python3 tools/os88bochs.py boot  [options]      # boot and leave it running
python3 tools/os88bochs.py sweep [options] PKG.o88 ...
python3 tools/os88bochs.py dbg   [--out DIR] "CMD" ...  # break in, run, continue
python3 tools/os88bochs.py stop                  # end every Bochs on the box
```

| option | effect |
|---|---|
| `--out DIR` | where the disks, bochsrc, `e9.out`, `bochs.log` and screenshots go (default `build/bochs`) |
| `--sb` | the SB16 above |
| `--ne2k` | the NE2000 above |
| `--cfg BITS` | write a SYSTEM.CFG onto A: wanting drivers, one bit each by `kernel/driver.inc`'s `drv_cfgbit`: **bit 0 SOUND.DRV, bit 1 HDD.DRV**. `--cfg 1` with `--sb` is a sound machine; `--cfg 2` gives the Hard Drive page its driver |
| `--c run` | (default) C: is built fresh: MBR, kern_mega's boot record and kernel, and a `RUN/` folder holding MEGARUN as `0RUN.O88` plus any packages named |
| `--c blank` | C: is 20MB of zeros - for the installer (Format, then Install) |
| `--c keep` | C: is `OUT/c.img` exactly as the last boot in that OUT left it |
| `--bootc` | boot from C: instead of A: |
| `--noa` | no disk in A: |
| `--b IMG`, `--btype` | B:'s image and drive type |
| `--a IMG` | A:'s image in place of the tree's `mega.img` - a kern_mega system disk with something changed, such as `make megacdtest`'s |
| `--cd ISO` | an ATAPI CD-ROM, the secondary master at 170h, holding an ISO 9660 image (CDROM.DRV, SPEC.md 119). **Data only**: Bochs 2.7 answers PLAY AUDIO and a SubQ READ SUB-CHANNEL "not implemented", so `build/megacdsim.img`'s `-DCDSIM_AUDIO` driver is what gives CDRack a TOC here, and 86Box's `vm/286-mega-cdrom` is where a disc plays (KERN-MEGA-PLAN 14.68). `--cfg 0x101` wants SOUND.DRV and CDROM.DRV |

`boot` returns once Bochs is up and leaves it running; the machine outlives the
command. The desktop takes about **80 seconds**. Wait for it by condition, not
by a fixed sleep:

```
until grep -aq "kern_mega: protected mode" build/bochs/e9.out; do sleep 3; done; sleep 15
```

## Driving it: tools/os88bochsui.py

```
python3 tools/os88bochsui.py capture         # once per boot: grab the mouse
python3 tools/os88bochsui.py goto X Y        # absolute, in GUEST pixels
python3 tools/os88bochsui.py move DX DY      # relative, guest pixels
python3 tools/os88bochsui.py click | dbl
python3 tools/os88bochsui.py down | up
python3 tools/os88bochsui.py key Return      # xdotool key names
python3 tools/os88bochsui.py type 'text'
python3 tools/os88bochsui.py shot out.png    # the guest's 640x480 (plus status bar)
python3 tools/os88bochsui.py e9 [OUTDIR]     # the tail of e9.out
```

`goto` pins the pointer at the top-left first (the kernel clamps it there),
then walks, because Bochs moves the guest one pixel per two X pixels. So `goto`
is absolute and repeatable, and costs a second or two. **A menu pick** is
`goto` the title, `down`, `move` to the item, `up`.

Coordinates on the 640x480 desktop that every recipe below uses:

| target | guest x, y |
|---|---|
| A: / B: / C: drive icons | 591,45 / 591,105 / 590,160 |
| System menu (the chip) | 10,5; Control Panel is +35 below it, Restart +99 |
| Control Panel page list | x 195; Hard Drive at y 267, Display at 197 |
| first rows of a fresh Disk window | x ~150, y 123, 139, 155, ... (16px a row) |

Take a `shot` and read it before trusting a coordinate in a window you have not
opened before.

## Reading it

* **`OUT/e9.out`** - the kernel's own trace. A `kern_mega FAULT vector VV
  stack: ...` line is a fault with sixteen stack words and the registers;
  MEGARUN names every package before it starts one. The menu-bar diagnostics
  (`NMI`, `RST`, `FDC RESET`, `RFL`, `Q`, `DOSP`) are echoed here too - on a
  `MEGADIAG=1` build (`make megadiag`), the only one that draws them since
  KERN-MEGA-PLAN 14.65.
* **`OUT/bochs.log`** - Bochs's own errors. A triple fault reads
  `exception(): 3rd (13) exception with no resolution, shutdown status is
  0ah, resetting`; one of those per reflection is NORMAL on the 286 path, which
  Bochs does not take anyway (blind spot 2).
* **`dbg`** - breaks in with SIGINT, runs Bochs debugger commands, prints what
  they wrote and continues. Kernel memory is at **linear 0x600 + the symbol's
  offset** (`KERNEL_SEG` is 0x60):

  ```
  grep -E " mega_rstn$" build/megak/kernel.map     # Virtual column = offset
  python3 tools/os88bochs.py dbg "xp /4bx 0xD2F7"
  python3 tools/os88bochs.py dbg "setpmem 0xD0F6 1 1"   # poke a byte
  python3 tools/os88bochs.py dbg "r" "sreg"             # registers, selectors
  ```

  **Take the map from the SAME build.** A failed `make mega` truncates
  `kernel.map`, and a test build with extra code moves every `.bss` offset
  after it. Both have produced plausible wrong numbers here.
* **A fault's CS:IP**: CS 0060 is `.text`, CS `COLD_SEG` (`kernel.asm`'s
  `COLD_HOME`) is `.cold` at its vstart, 80h on kern_mega since KERN-MEGA-PLAN
  14.45 (4000h before it), and CS `FFD8` is `.mega` at 0. A CS with bit 2 set is an LDT
  code alias, whose image is segment `(CS & ~7) << 2`. Resolve its IP against
  that image's own `nasm -l` listing, assembled with the same defines as the
  shipped one (SOUND.DRV, for instance, is `-DPICOMEM`).

## Recipes

**Boot to the desktop and look.**
```
python3 tools/os88bochs.py boot
until grep -aq "protected mode" build/bochs/e9.out; do sleep 3; done; sleep 15
python3 tools/os88bochsui.py shot desk.png
```

**Start every package and charge each fault to the package that caused it.**
`sweep` builds C: with MEGARUN and a batch of packages, opens C:\RUN, starts
each one five seconds apart, and moves on to the next boot at a fault or a
minute of silence. A screenshot goes in OUT for every start.
```
python3 tools/os88bochs.py sweep --batch 6 build/*.o88
```

**Sound.** `boot --sb --cfg 1`, open B: (591,105), MEDIA, then double-click
BEVERLY.MOD (Tracker) or a WAV. `OUT/sb.wav` grows while it plays.

**Copy a file C: to B: and check it.** Put a file in C:\RUN by naming it
after `boot` (`--c run` stages the arguments), copy it with the Disk window's
Edit menu, then compare on the host:
```
python3 tools/os88fat.py cat -o got.bin build/bochs/b.img NAME.BIN && cmp want.bin got.bin
```

**The hard-disk installer, and boot from what it wrote.**
```
python3 tools/os88bochs.py boot --c blank --cfg 2 --out build/bochshd
#   Control Panel -> Hard Drive -> Format (click twice), Close, Install,
#   Install (click twice); wait for "Done - remove the floppy, Restart"
python3 tools/os88bochs.py stop
python3 tools/os88bochs.py boot --c keep --bootc --noa --out build/bochshd
```

**Restart.** System menu -> Restart. kern_mega warm-resets through the 8042,
and the second `kern_mega: protected mode` in e9.out is the proof.

**Count the #GP fixups: the trap census** (KERN-MEGA-PLAN 10.3, items
55-62). Every fixup is correct and costs a fault, so a loop that leans on one
is slow without looking broken. `make megatrap` builds the census kernel into
`build/megatrap/` (`MEGATRAP=1`: one E9 line per #GP - error, CS:IP, DS, ES,
and the name in the image's header), and `--build` boots that tree:
```
make megatrap
python3 tools/os88bochs.py boot --build build/megatrap --sb --cfg 1 \
    --b MYDISK.img --btype 1_44 --out build/census/x
#   ...open the program and USE it: starting one traps almost nothing
python3 tools/megatraps.py build/census/x/e9.out --top 20
```
`megatraps.py` ranks the sites, names the fixup that took each (`xfer`,
`write`, `null0`) and resolves the IP in the image's own listing - by header
name, or `--lst 1600=part.lst` for a nameless image such as a PiXEL part.
A `sweep --build build/megatrap --linger 60` leaves a boot running after
MEGARUN's last start, for the loops a program runs on its own.

**Count something per second without a debugger.** The kernel's `mega_rfsec`
(once a second, from the tick) and `mega_tell` (from `ui_task`, the only
place a toast may be staged) are the pattern. A new counter belongs there, not
in an ISR that draws - and inside `%ifdef MEGA_DIAG`, since a shipped
kern_mega puts no field reading on the glass (KERN-MEGA-PLAN 14.65).

## Things that cost an hour each here

* **ONE Bochs per DISPLAY.** Clicks, keys and screenshots go to whichever
  window is on top of the display, and two machines on one read exactly like a
  guest that froze. `boot` refuses while one runs ON ITS DISPLAY. Two at once
  is two displays: `OS88_BOCHS_DISPLAY=:97 tools/os88bochs.py ... --out
  build/bochs-<name>` (and the same variable for `os88bochsui.py`). `stop`,
  and the `already running` check, count only the bochs-bin and fifo holders
  whose environment names this display - they used to be box-wide, which
  ended every other agent's machine. `pkill bochs` does NOT match: the
  process is `bochs-bin`, so use `os88bochs.py stop`.
* **Capture the mouse after every boot**, or moves go nowhere.
* **The harness copies A: from `build/mega.img` every boot.** Rebuild with
  `make mega` before you boot, or you test the previous kernel.
* **A throwaway test build must be reverted.** The pattern used here: copy
  the file aside, patch, `make mega`, copy it back, check `git diff`, and grep
  for your `TEST ONLY` marker. Then rebuild before you ship anything.

## Blind spots: where Bochs does not stand for the Victory 286

1. **It cannot run two machines at once** (above).
2. **Its CPU is a 386**, so a reflection leaves protected mode by clearing
   CR0.PE. The 286 path - a CPU RESET through the BIOS's shutdown code
   (CMOS 0Fh = 0Ah), back at `mega_r286` - does not run under Bochs, and
   forcing it does not help: Bochs's triple fault resets the WHOLE machine,
   devices included, where an AT resets only the CPU. A forced-286 build hangs
   opening C:. Two field defects lived only on this path (KERN-MEGA-PLAN 10.3
   item 24 is one). Whatever touches `mega_r286` was the field machine's to
   test, and is now **86Box's first** (docs/86BOX.md): `tools/os88box.py
   launch 286-mega` is the owner's mr286, a real 286 whose shutdown resets
   only the CPU, driven headless - and its `faults` verb is what found
   KERN-MEGA-PLAN item 88, a boot loop that existed only on a 286 BIOS.
3. **HDD.DRV takes rung 1, native IDE,** on any 286 or later, so a Bochs C:
   never goes through the BIOS. On the Victory, C: is a BIOS-only PicoMEM and
   every C: call is a reflection. To exercise BIOS-backed C: under Bochs,
   build a throwaway HDD.DRV whose rung-1 probe is skipped
   (`drivers/hdd/hdd.asm`, the `cmp al, CPU_286` before `hd_at_channel`).
4. **There is no PicoMEM.** Its Sound Blaster is an option ROM's multiplexer
   on one IRQ line, raising the SB's IRQ as a software `int 8+n` and copying
   each DMA block itself (KERN-MEGA-PLAN 10.3 items 18-24). A Bochs SB16 is a
   plain card. The reflection machinery can still be exercised by a throwaway
   build that routes IRQ 5 through `mega_irv` and replays it to the driver's
   alias (item 18 has the shape), but the card's own behaviour cannot be.

5. **Its floppy controller ignores the data rate.** Every floppy mount's
   first read (LBA 0, at the 9-sector fallback geometry) makes `mega_fd13`
   guess 250 kbps, which a real 765 refuses for a 1.44MB disk and the BIOS
   then reads through a reflection. Bochs reads it natively, so no run here
   prints `FD BIOS` (a `MEGADIAG=1` toast; E9's `kern_mega: floppy fell back`
   line on every build) and that fallback is the field machine's to exercise
   (KERN-MEGA-PLAN 10.3 item 29).

The field machine is the owner's Packard Bell Victory 286 (docs/FIELD-MACHINES.md).
Anything in the five blind spots has a menu-bar diagnostic on the
`MEGADIAG=1` build (`make megadiag`, KERN-MEGA-PLAN 14.65) and a line saying
what a photograph of it should show; the shipped kern_mega draws none.
