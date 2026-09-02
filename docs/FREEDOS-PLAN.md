# FreeDOS as a guest, and hibernation to the hard disk — the plan

**Plan document, not a contract.** SPEC.md §86 is the contract for what has
shipped on the `freedos-package` branch; every wave below updates SPEC.md
*before* its code, and where this file and SPEC.md disagree, SPEC.md wins.
Every figure here was measured on this tree at `d3b68d9` (the branch with
`main` merged in), and the ones that are estimates say so.

The ask, in the requester's words:

> In order to make this FreeDOS branch code more useful, I want to add
> hibernation to disk support, so that we can hibernate to a hard disk and
> then run FreeDOS. Make FreeDOS support officially supported, fill in the
> feature gaps to allow hibernate to hard disk, and have FreeDOS as a guest
> operating system on the hard disk. Then create an 86Box configuration that
> would let me test this support.

---

## 0. The verdict, up front

**The branch has the hard half already and it is right: os8088 and FreeDOS
are two BOOTS of one machine (§86.7), and the handover is a torn-down machine
and a 127-byte stub at `0000:0500`.** Nothing below changes that shape. What
is missing is everything around it: the session is *lost* on the way out,
the DOS side lives on a floppy in B:, the build only works on the machine it
was written on, and no test anywhere exercises any of it.

Four waves, in dependency order. Wave 0 is what makes the *floppy* FreeDOS
official; wave 1 is hibernation on its own, with no DOS in the frame; wave 2
puts FreeDOS on the hard disk and makes the round trip; wave 3 is the field.

| wave | delivers | resident kernel cost (estimate) |
|---|---|---|
| **0 — official** | a pinned, host-independent `make dos`; the text-mode screen reader the tests need; `chainb` built and run; two QEMU rows (`dosgate`, `dosboot`); the two **hard-disk 86Box machines** (`xt-dos-hdd`, `386-dos-hdd`) and the pre-installed `build/dos-hdd.img` they boot — **built in this session, §8** | 0 |
| **1 — hibernate** | `HIBERNAT.SYS` on the boot partition; `HIBER.DRV`, an on-demand module (§2.8) that writes the image and re-establishes the machine after a restore; the boot-time check in the overlay; the restore stub in the `0x500` page; **Sleep** (hibernate, then halt) and a restart-that-resumes; `OSAPI_HIBER` | `.text` ≤ ~120 B, `.cold` ~250 B, `.ovl` ~250 B (**one more blob sector**), module ~1.5–2 KB, off-budget |
| **2 — FreeDOS on C:** | FreeDOS in `C:\DOS`, started by os8088's own loader through a second mode of the stub; the FREEDOS window's second button, *Hibernate, then start FreeDOS*; `OS8088` at the DOS prompt brings every window back; the DOS payload on the live media | `.cold` ~60 B (the stub's second mode), module ~300 B, package ~300 B |
| **3 — field** | numbers from a real XT-IDE 8088; the XMS section of the image when a consumer of the pool exists; a resume from a floppy boot; PERFORMANCE.md Part 5 rows | — |

The **five decisions** the rest of this document argues, each with the
alternative it beat in §6:

1. **A resume is "restore RAM, then run the tail of `kmain`."** Every hook
   the kernel installs lands at the same address on every boot of the same
   build, so the fresh kernel's IVT is already the hibernated kernel's IVT;
   the scheduler keeps every task's frame in `.lowbss`; there are no open
   file handles because the file API is whole-transaction by name
   (§18/§19); and `drv_shutdown` already *unloads and frees* every driver on
   the way to a restart. So the restored kernel does what a booting kernel
   does after `drv_boot`'s settings are in: load the wanted drivers, attach
   `XMEM.DRV`, one `wm_paint_all`, `jmp ui_task`. Nothing is "suspended";
   the teardown is the reboot's and the bring-up is the boot's.
2. **The image is a preallocated, contiguous, hidden system file on the
   boot partition, and the boot partition must be a hard disk.** Hibernate
   greys on a floppy-booted machine — a *fact* (`[dsk_bootvol] & 80h`),
   never a guess (§47) — and a machine booted from the live USB stick is an
   installed machine and gets it for free (§80.1). The file is found by
   name (`HIBERNAT.SYS`) and read by the boot sector's own arithmetic.
3. **Nothing of size is resident.** The writer and the post-restore half
   live in `HIBER.DRV`, a kernel module cut out of this build (§2.8) — they
   name kernel internals and run once per session, which is exactly what a
   module is for. The boot-time check is overlay code (§2.5) and costs the
   blob its 14th sector (§2.9.6 predicted that claim; ~24 ms a boot). The
   stub that reads the image over the running kernel is the one thing that
   must stand outside the image, and the `0x500` page is the one page that
   is neither the BIOS's nor ours. The kernel segment has **1,988 bytes**
   left on this branch (`tools/kernsize.py`), and this plan spends under a
   tenth of it there.
4. **FreeDOS on the hard disk lives on os8088's own partition, in
   `C:\DOS`, and os8088's loader starts it.** FreeDOS's boot sector is never
   installed and `SYS` is never run. FreeDOS's kernel is entered at
   `0060:0000` with `BL` = the unit and reads nothing else (§4.2); it calls
   any unit ≥ 80h "C:" and gives C: to the active primary FAT partition on
   the first disk — which is the partition os8088 installed. Both systems
   see one volume, which is what makes a file saved in Word visible to
   `DIR` and back. The only rule is the folder: FreeDOS's `KERNEL.SYS`
   cannot share a root with os8088's, because that is the name
   `boot/boothd.asm` loads (§52.10.2).
5. **The way back is unchanged**: `OS8088.COM` warm-boots, the ROM boots
   C:, the fresh kernel finds a complete image and resumes. Wave 1's
   restart-that-resumes is the same path with no DOS in it, and it is the
   test.

What this plan **refuses**, stated so nobody spends a week on it: a resume
that survives a *different* kernel build (the image carries the two stamps
§2.8.2's modules carry and is refused on either); a resume onto a machine
with a different `int 12h` answer; saving the XMS pool before anything
allocates from it (kernel/xmem.inc:27-32 — nothing does); and any form of
DOS-in-a-window (§86.7 already closed that).

---

## 1. Where the branch stands

Facts, so that the gaps in §2 are gaps and not guesses. `git diff --stat
main...HEAD`: one feature commit, 24 files, +1,418.

- **The handover** (kernel/ui.inc `ui_handoff`/`ui_hstub`, SPEC.md §86.3):
  `ui_cmd_reboot` verbatim — `cp_flush_close`, `drv_shutdown`, `gfx_lock`,
  `vid_reboot`, `sched_unhook` — then, if a unit was posted, the diskette
  parameter table goes back to `DPT_AT` (0x0580), the stub is copied to
  `HANDOFF_AT` (0x0500) and entered with `DS = 0`. The stub resets the unit,
  reads its first sector to `0000:7C00`, checks `AA55`, and jumps with `DL`
  = the unit. The size guard is `DPT_AT - HANDOFF_AT - 1` = **127 bytes**.
  An empty drive falls to `int 19h`.
- **The unit is a parameter** (§86.4), but the stub reads cylinder 0 head 0
  sector 1 of it and jumps there — on a hard disk that is the MBR, and
  nothing chain-loads a volume boot record. Unit 80h is *named* as future
  work and does not work today.
- **`OSAPI_BOOT_UNIT`** (slot 0x04F0, `ui_boot_post`) stores unit+1 and
  posts `UI_RBQ_FLUSH`; `ui_reboot_post` clears the unit so a cancelled
  FREEDOS window cannot ride a later Restart out.
- **The FREEDOS package** (apps/freedos/freedos.asm, 270 lines) is a window,
  three lines of warning and one button, on the **system disk's root**
  because in a `*-dos` configuration the apps disk is not in the machine.
- **The DOS volume** is `tools/os88disk.py --dos`: FreeDOS's own FAT12 boot
  sector (patched CHS-only at offset 0x17B, asserted before poking),
  `KERNEL.SYS` via `--kernel` so it is contiguous from cluster 2,
  `COMMAND.COM`, `FDCONFIG.SYS` tuned for an 8088, `AUTOEXEC.BAT`, and
  `OS8088.COM` — a warm boot, `1234h` at `0040:0072`, for §86.5's reason.
- **The build** (`tools/build-freedos.sh`) compiles FreeDOS's kernel and
  FreeCOM with Open Watcom's Apple-Silicon host binaries, from **sibling
  checkouts `../kernel` and `../freecom` that are neither fetched nor
  pinned**. On this machine they do not exist beside this worktree (they are
  beside the one the branch was written in, `~/Repos/dos-package/`), so `make
  dos` fails here with a `cp` error. §2.1.
- **Tests: none.** `tests/chainb/chainb.asm` is the capability gate §86.7
  names, and nothing builds or runs it (`grep -n chainb Makefile` is empty;
  `tests/unit/t_registry.py` does not walk subdirectories, so it neither
  requires nor exempts it). `docs/TESTING.md` was extended with the one
  question 86Box answers (does the XT BIOS report two floppies), and that
  question has not been asked.
- **Hard disks: nothing.** No `vm/*/86box.cfg` in the tree attaches one;
  §52.10 was verified under QEMU (`HDD=`), MartyPC (`os8088_xt_hdd`, XT-IDE)
  and on field machines.

---

## 2. Wave 0 — what "officially supported" means here

### 2.1 A build that works on a fresh clone

`tools/getfreedos.py`, in the shape of `tools/getruncpm.py` (pinned commit,
per-file SHA-256, hard-fail on mismatch, a stamp file the Makefile depends
on, output under `build/`, never committed):

| input | pin | why |
|---|---|---|
| `FDOS/kernel` | `d6791add2043c9d7b584d840a8ffaf8829fd2bdc` (2026-07-01) | the tree the branch was proven with |
| `FDOS/freecom` | `04fc21a9f6792abe9048598e8f2d048b4f6cd0e5` (2026-05-21) | same |
| `FDOS/country` | `7f83e041d00f78b3912c761246930f3b437440f6` | already pinned in the script; `config.c` `#include`s `../country/kernel.tb1` outright |
| Open Watcom V2 | a **dated release tag**, not `Current-build` | the script's own comment says the URL is rolling and its hash is advisory; every other fetcher here hard-fails, and this one should too, on a tag that cannot move. The script tries the pinned tag first and says which it used |

Three portability faults in the shell script, each one line: `armo64` is
the only host build it unpacks (add a table: `armo64` Apple Silicon,
`osx64`/`binl64` for an Intel Mac and Linux); `shasum -a 256` is BSD (fall
back to `sha256sum`); `sed -i ''` is BSD (the Makefile's own rule is `perl
-pi`, Makefile:6869). `make clean` must spare `build/freedos` the way it
spares `build/cc` — a 148 MB pinned upstream instrument is not a build
product. `getfreedos.py --check` for the verify-only mode the others have.

The two committed modifications (`patches/owosx.mak`, the FreeCOM `-DGCC`
sed) stay exactly as they are; `tools/freedos/README.md` already argues both
and the arguments are good.

### 2.2 Tests that see a text screen

FreeDOS lives in a text mode, and **nothing in the tree reads one under
QEMU**: every screenshot reader decodes a graphics framebuffer, and the OCR
in `tests/rczex_ocr.py` learns os8088's own 8×8 glyphs off a P6 dump. MartyPC
has a first-class `screen` verb (tools/os88marty.py:163). QEMU has the
primitive and nobody has used it: `pmemsave 0xB8000 4000 <file>` through
`tools/qmp.py` gives the character/attribute buffer (tests/ethernet.py:189
and tests/trkscrl.py:97 already use `pmemsave` for other addresses).

- **`tools/qmptext.py`** — the twenty lines that turn that dump into 25 rows
  of text, and a `wait_for(sock, pattern, secs)`. Every DOS row below is
  built on it.
- **`make chainb`** builds `build/chainb.bin` (512 bytes, `org 0x7C00`) and
  **`dosgate`** (QEMU, soak) boots it as A: with `dos360.img` in B: and waits
  for `B:\>`. It answers §86.7's question with nothing else in the frame.
- **`dosboot`** (QEMU, soak) boots the system disk with the DOS floppy in
  B:, opens FREEDOS off the desktop, clicks *Start FreeDOS*, waits for
  `B:\>`, types `OS8088`, and waits for the desktop again — asserting the
  return through the BDA video mode byte (`0040:0049` = 12h) and a screenshot.
  This is the row that would have caught a broken `OS8088.COM`.
- A **`dos` capability** in `tools/os88test.py`, probed like `cc` is
  (`build/dos/KERNEL.SYS` exists), so the rows SKIP loudly on a tree that
  has not run `make dos` and never fail it.
- `t_registry` learns `tests/chainb/` exists (an `UNREGISTERED` entry, or
  the walker learns one subdirectory).

### 2.3 The hard-disk machines — built, §8

`build/dos-hdd.img`, `vm/xt-dos-hdd`, `vm/386-dos-hdd`, `make xt-dos-hdd` /
`make 386-dos-hdd` with `HDBOOT=1`. §8 is the account, including what was
verified and how the 86Box keys were established.

### 2.4 Documents

SPEC.md §86.8 (the hard-disk machines — written with this plan);
docs/TESTING.md's 86Box matrix; README's target list and its FreeDOS
section; CLAUDE.md's target list. One drift found on the way: SPEC.md
§80.1 still says `boot/boothd.asm` takes its geometry from `int 13h AH=08h`,
and §52.10.2 and the file say the BPB — fix it in the same commit.

---

## 3. Wave 1 — hibernation

### 3.1 The image

**`HIBERNAT.SYS`**, in the root of the boot partition, hidden + system +
read-only, **contiguous**, sized once for the machine and never resized by
the writer. It is DOS's `hiberfil.sys` shape for DOS's reason: the code that
reads it back runs before there is a file system to walk, so the file is a
run of sectors with a name.

```
sector 0  the header, 512 bytes, written LAST
  +0   db 'HIB8'            signature
  +4   dw BUILD_NUM         §14.2 - the commit
  +6   dw MOD_STAMP         §2.8.2 - the build of the commit (KTEXT+KBSS+COLD+KLOW)
  +8   dw HEAP_SEG          the ladder, belt and braces
  +10  dw mem KB            int 12h's answer when the image was taken
  +12  dw sectors           of body, = (memKB*1024 - 0x600) / 512, rounded up
  +14  db state             0 = empty, 1 = COMPLETE, 2 = CONSUMED
  +15  db target            what the writer did next (halt/restart/unit) - for the About box's curiosity, nothing reads it
  +16  dd body LBA          absolute (partition base added), computed while the FAT was live
  +20  dw skip sector       the one body sector the stub does not read (§3.3)
  +22  dw header sum        16-bit additive over +0..+21, so a torn sector is a refusal
sectors 1..N  linear 0x00600 .. memKB*1024, in order
```

**Size.** A 640 KB machine images 654,848 bytes = **1,279 sectors** +
header = 1,280 sectors = 640 KB exactly; a 256 KB machine 511. The file is
created at the machine's size by whoever installs — `hd_inst_sys` right after
`KERNEL.SYS` (§52.10.4 step 2; a fresh volume allocates forward, which is
the same argument that makes `KERNEL.SYS` contiguous), `tools/os88disk.py
--hdd --hibernate <KB>` for the host-built images (default 640, the live
media's, §80), and **on demand from the refusal**: a machine installed
before this wave has no file, and the toast that says so offers to create
one, through `OSAPI_FILE_WRITE_SYS` and a verified chain. The writer walks
the chain every time and refuses a fragmented file with the reason; it
never repairs one.

**Not in the image**: the `0x500` page (the stub's), the BIOS data area
(POST's), video RAM (repainted), the XMS pool (§0's refusal — re-attached
empty, and the header gains a section the day a consumer exists).

### 3.2 Going down

The post is `ui_task`'s and the teardown is `ui_cmd_reboot`'s, which is the
branch's own argument for the handover (§86.3) applied a second time.

```
 ui_cmd_reboot, as it is           ...with a hibernate posted
 1 cp_flush_close                  1 cp_flush_close
                                   1a mod_need MOD_HIBER; PREFLIGHT: the file, its chain,
                                      its size, the boot volume is 80h.  A refusal here
                                      TOASTS AND RETURNS - nothing has been torn down yet
 2 drv_shutdown                    2 drv_shutdown          (frees every driver, §51)
 3 gfx_lock                        3 gfx_lock
 4 vid_reboot                      4 sched_unhook          (single-threaded from here)
 5 sched_unhook                    5 WRITE THE IMAGE, with §12.8's progress widget on the
                                      desktop that is still on the glass; header last
 6 handoff / int 19h               6 vid_reboot
                                   7 target: halt ("Safe to switch off"), int 19h, unit 1's
                                      stub, or unit 80h's (wave 2)
```

Two orderings are load-bearing. **The preflight is before `drv_shutdown`**
because every refusal must leave a desktop to refuse on (§2.8.4 — the
caller owes the reason). **The write is after `sched_unhook`** because the
image has to be a snapshot: with the scheduler live, a worker task mutates
memory under the writer. After the unhook the machine is single-threaded
and the only thing that moves is the writer's own state, which the resume
never reads. The writer therefore issues **its own `int 13h AH=03h` loop**
over the run — the same track-and-64KB-page bounds as `hd_bios_run`
(drivers/hdd/hdd.asm:786) — and not `dsk_xfer`, which is written for a
scheduled machine. The run's absolute LBA was computed at step 1a while the
FAT machinery was live and is the number the header carries.

`vid_reboot` moves below the write so the progress bar has a screen. It is
a mode set and depends on nothing the unhook took.

**Time.** 1,280 sectors at rung 0. On an 8-bit XT-IDE the transfer is PIO
through the CPU, measured elsewhere at roughly 100–150 KB/s: **5–7 s**. An
MFM controller's ROM, 60–90 KB/s: **8–10 s**. A 386 with a 16-bit IDE, under
2 s. QEMU, instant and useless as a number (PERFORMANCE.md rule 4). A
progress bar is therefore not decoration; it is what stops the user
switching the machine off at 60%.

### 3.3 Coming back

**Where in `kmain`.** After `mouse_init` (MARK 19) and `mem_init` (MARK 12)
and before `drv_boot` (MARK 29) — kernel/kernel.asm:4361 — on the line
`SPLGATE splf_step` at MARK 28. Earlier and the mouse's UART is not
programmed and the fresh IVT hooks are not all in; later and `drv_boot` has
loaded drivers whose IRQ vectors point into heap the restore is about to
overwrite. At MARK 28 the fresh kernel has installed int 08h, int 09h/0Ch,
int 1Eh — all at addresses the restored kernel shares, because it is the
same build — and has loaded nothing off the disk but `SYSTEM.CFG`'s reader.

**`ovl_hib_check`** (overlay, ~250 bytes): `[dsk_bootvol] & 80h` or return;
find `HIBERNAT.SYS` in the root through the resident name lookup the module
loader uses (`mod_need` goes to `[dsk_bootvol]` and only there, §2.8.4);
read sector 0; refuse on signature, either stamp, `HEAP_SEG`, the memory
size, the sum, or `state != COMPLETE`. Then **consume**: write the header
back with `state = CONSUMED` — before the restore, so a restore that dies
halfway leaves a machine that boots cold next time rather than one that
dies halfway every time. Copy the stub to `0x0500` with its parameters
(body LBA, count, skip sector, `[dsk_bootspt]`/`[dsk_boothds]`, the unit),
`cli`, `jmp 0:0500`.

**The stub** (`.cold`, copied out; ~130 bytes; the same page the handover
stub uses, at a different moment — the two never coexist and share
`HANDOFF_AT`): `DS = ES = 0`, a stack (below), then a loop of `AH=02h`
reads, each bounded by the track and the 64 KB page, over linear
`0x00600 ..`, skipping the one sector the header names, three attempts with
an `AH=00h` reset like every other loader here. On the last sector it
`jmp`s to a fixed address: `COLD_SEG:hib_resume`, which the same-build rule
guarantees is there.

**The stack problem, and the skip sector.** The stub calls the BIOS, and the
BIOS needs a stack, and every byte from `0x600` up is about to be
overwritten. The `0x500` page has 256 bytes and the stub is in it; the top
of RAM is heap (§2.7: the first package loaded sits where the boot sector
was); there is no reserved region anywhere (docs/KERNEL-MEMORY.md, and
report §1 of this session). The answer is that **one region of the image
does not need restoring: task 0's stack**, `STK0_BOT..STK0_TOP` in
`LOW_SEG` (1,024 bytes, kernel/kernel.asm:1918), because the resume enters
with a fresh `SP = STK0_TOP` and never returns into the hibernated frames —
they are `ui_cmd_reboot`'s. A 1,024-byte region always contains one
512-aligned sector; the stub's stack is that sector's top, and the stub
skips exactly it. The writer writes it anyway (simpler), the header says
which. The alternative — trusting the BIOS's `int 13h` to fit in the
`0x500` page's spare 120 bytes — is a bet on every ROM in
docs/FIELD-MACHINES.md, and XUB is documented to switch to its own stack
only when configured to.

**`hib_resume`** (`.cold`, ~120 bytes): `SS:SP = LOW_SEG:STK0_TOP`,
`DS = ES = KERNEL_SEG`, `cld`; **`sched_rehook`** (new, ~30 bytes: int 08h
→ `sch_isr` and PIT channel 0 to mode 2 / divisor 0, which is `sched_init`
minus the table clear it must not do — there is no such routine today,
kernel/sched.inc:302 unhooks and only `sched_init` installs); `sti`;
`vid_setmode` for the restored `[vid_kind]` and the second card
(kernel/viddet.inc:758 — the fresh boot left the splash's mode, and §39.12's
context is in the image); then the module's resume half, **already loaded**
— the restored `mod_fp` slot points at the restored image, so this is a
`call far [mod_fp + K]` with no `mod_need` — and finally the boot's own
ending: `gfx_lock` / `wm_paint_all` / `gfx_unlock` (the lock was held at
the write and is still held), `cursor_show`, `drv_notice_x`, `jmp ui_task`.

**The module's resume half**, in order:

1. `mem_free` the hibernated writer's transients (the progress widget's, the
   FreeDOS kernel buffer of wave 2).
2. **Invalidate every disk cache.** `dskw_refat` (one word), the read-ahead
   table (`dsk_rah_*`, §18.95), the banked BPB's `dsk_bpbok` for the boot
   volume (1 = "good for ever" on a fixed disk, §18.9.2, and still true —
   but the FAT window behind it is not), and `dskw_sync_x` so every Disk
   window re-lists. Another operating system may have written to this volume
   in between, and the FAT window is a window, not a snapshot (§18.8). The
   floppies get the same for the cheaper reason that a disk may have been
   swapped, and §18.8's signature already refuses a mismatched one.
3. **Re-read the clock.** The RTC is read once at boot and advanced from
   the PIT (kernel/clock.inc:5); the machine has been off or in DOS for an
   unknown time. The probe-and-read ladder is overlay code and the overlay
   is gone — so the module `%include`s the ladder's source, the way
   `clockw.inc` is `%include`d into `CTRL.DRV` (§37.94): shared as source,
   never as a copy. An alarm that came due while the machine slept fires on
   the next tick, which is correct.
4. **Load the wanted drivers**: the loop in `drv_boot_x`
   (kernel/driver.inc:3929, `DRVR_FILE != 0 && DRVR_WANT != 0` → `drv_load_x`),
   factored so both callers share it. The rows and their WANT bits are in
   the image; `SYSTEM.CFG` is not re-read. `HDD.DRV` re-mounts what §52.6
   remembered, `ETHER.DRV` re-acquires an address, `SOUND.DRV` is silent —
   exactly a boot's outcome, which every package already survives (a
   package reaches a driver by class on every call, kernel/driver.inc:4327,
   and gets CF on a class nobody publishes).
5. `xmf_xm_boot` — `XMEM.DRV` attaches empty (§0's refusal).
6. `mod_drop MOD_HIBER`.

What that leaves as **lost, by design**: every TCP connection and the DHCP
lease (the driver was unloaded, as on a restart); a sound clip in flight;
the clipboard is *kept* (it is heap); the Control Panel's unsaved edits
are *kept* (the panel window is heap and its module is reloaded on the next
click, §2.8.3 — `cp_flush_close` at step 1 wrote what was worth writing).

### 3.4 Hazards, each with its fence

| hazard | fence |
|---|---|
| a different kernel build | `BUILD_NUM` + `MOD_STAMP` in the header — the two stamps modules already carry (§2.8.2); either mismatch refuses |
| a different memory size (a DIP switch, a removed board) | `int 12h` in the header; mismatch refuses |
| power lost mid-write | `state` is written last; anything but COMPLETE refuses |
| a crash mid-restore | the header is CONSUMED **before** the stub runs; the next boot is cold |
| DOS wrote to C: while we slept | §3.3 step 2 — every cache invalidated; the file API carries no state between calls (§18.4.4), so a name DOS deleted answers `FERR_NOENT` on its next use, honestly |
| DOS deleted or moved `HIBERNAT.SYS` | the writer walks the chain every time; the resume finds no file and boots cold |
| the `0x500` page is `XMEM.DRV`'s A20 scratch too (drivers/xmem/xmem.asm:312) | ordering: the stub is dead before MARK 30's attach, and the page is scratch to both, never state |
| the BIOS's stack | the skip sector, §3.3 |
| a warm reboot on an emulator keeps RAM, so a restore that reads nothing "works" | the test reboots by killing QEMU and starting a fresh process (zeroed RAM), never by `system_reset` |
| an image on a floppy-booted machine's hard disk | not consulted (wave 3 may) — the rule is one line and it is the boot volume |

### 3.5 What the user sees

- **System menu: `Sleep...`** — a sixth item (kernel/menu.inc:2557 has
  five, `MENU_LOGO_N`), greyed by the fact `[dsk_bootvol] & 80h == 0` with
  §47's discipline (the reason is "Boot from the hard disk" and the About
  box could say it). It opens FREEDOS's shape of window: three lines, one
  deliberate button, the close box is Cancel. *Sleep* writes the image and
  halts with a text-mode line, `Safe to switch off. Switching on resumes
  where you left off.`
- The refusals, as toasts (§59): `Needs HIBERNAT.SYS on C:` (with the
  create-it path), `HIBERNAT.SYS is fragmented`, `HIBERNAT.SYS too small
  (needs 640K)`, `C: is read-only` (the CD, §80.3).
- A restart that resumes is **not** offered in the menu; it is the test's
  door (`OSAPI_HIBER` AL = 2) and `tests/hiber.py`'s whole method.

### 3.6 The contract to write into SPEC.md first

```
OSAPI_HIBER      KERNEL_SEG:0x04F8   AL = 0  query: out AL bit 0 = a hard disk
                                             boot, bit 1 = HIBERNAT.SYS present
                                             and whole, bit 2 = C:\DOS present
                                             (wave 2); CF clear always
                                     AL = 1  hibernate, then HALT
                                     AL = 2  hibernate, then int 19h
                                     both POST (the §20.10 rule); every
                                     register preserved; the refusal, if any,
                                     is the toast ui_task raises when it spends
                                     the post - a callback cannot be told,
                                     because nothing has been tried yet
OSAPI_BOOT_UNIT  (0x04F0, exists)    AL = unit; AH bit 0 = hibernate first
                                     (wave 2 adds unit 80h)
```

One slot, 8 bytes of table, and the `t_api_abi` alias line. A second
`OSAPI_SLOT` at 0x0500 stays free.

### 3.7 Costs, estimated against the guards

| where | bytes | note |
|---|---|---|
| `.text` | ~100 | the slot cell, the `ui_task` post byte's second reader, the menu item and its `CMD_*`, `sched_rehook` if not cold |
| `.cold` | ~250 | `hib_resume`, the stub as copied bytes (code, so it may live in cold), the preflight thunk |
| `.ovl` | ~250 | `ovl_hib_check`; the blob has 159 bytes of slack, so **`BOOT2_SECS` 13 → 14** — both files, one rung, ~24 ms a boot, §2.9.6 |
| `HIBER.DRV` | 1.5–2 KB | off every budget; a fourth `MOD_*` slot (`MODFP_STRIDE` words of `.bss`) |
| `.bss` | ~8 | the target byte, the run LBA |

Against `.text+.bss` 63,548 of 65,536 (1,988 left) that is comfortable, and
`KERN_BUDGET` has 8,704 spare. The one number to watch is the cold rung:
157 bytes left in the current step, so `.cold` +250 is a 512-byte rung, out
of the budget's spare. Measure with `python3 tools/kernsize.py` before and
after, both variants (the memory note in this repo's history says why).

### 3.8 Tests

- **`hiber`** (QEMU, soak): `make test HDD=...` with `build/dos-hdd.img`
  copied to `build/hdd.img` and a `BOOT=c` knob (the `test` recipe hard-codes
  `-boot a`); open Calc and drag it somewhere; `OSAPI_HIBER` AL = 2 through
  a test package, or the menu; assert the header on the host
  (`tools/os88flush.py`'s `Volume` reads a flat image; `state == COMPLETE`);
  **quit QEMU, start it again**; assert the Calc window is at the same place
  by crop. Then assert `state == CONSUMED`.
- **`hibcold`** (QEMU, soak): the same with the header's stamp poked on the
  host — the machine must boot cold and say nothing.
- **`hibxt`** (MartyPC, soak, `os8088_xt_hdd`): the cycle-exact 8088 writes
  the image; the number is PERFORMANCE.md's, and the row asserts the resume.
- The fast tier gains nothing but `t_api_abi`'s line and the doc gate.

---

## 4. Wave 2 — FreeDOS on the hard disk

### 4.1 The layout

```
C:\KERNEL.SYS        os8088's kernel - the name boot/boothd.asm loads
C:\HIBERNAT.SYS      wave 1's image
C:\FDCONFIG.SYS      the hard-disk edition (dos/hd/fdconfig.sys) - FreeDOS looks in the root
C:\AUTOEXEC.BAT      the hard-disk edition (dos/hd/autoexec.bat) - FreeCOM's /P looks in the root
C:\DOS\KERNEL.SYS    FreeDOS's kernel (kwc8616.sys) - a folder, so the two KERNEL.SYS never meet
C:\DOS\COMMAND.COM
C:\DOS\OS8088.COM    the way back, unchanged
```

`build/dos-hdd.img` already has this layout (§8), so wave 2 starts from a
disk that exists. The two root files are the floppy editions with `B:\`
replaced by `C:\DOS\` (`SHELL=C:\DOS\COMMAND.COM C:\DOS\ /P /E:512`), and
every line about an 8088 in the floppy edition holds unchanged.

**Why not a second partition.** FreeDOS on a partition of its own with its
own boot sector is the textbook install, and it would still see os8088's
partition as C: — `initdisk.c` gives C: to the *active* primary FAT
partition on unit 80h (SCAN_PRIMARYBOOT, `pEntry->Bootable`), which is
ours, and its own would be D:. So the shared-volume hazards (§3.4) are
identical, and what the second partition buys is a second FAT to keep
consistent, an MBR selector, `SYS` in the build, and a machine whose
FreeDOS is on a different letter from its files. Refused.

**Why not FreeDOS's boot sector.** The VBR slot is `boot/boothd.asm`'s,
and a boot sector that chooses between two kernels does not fit in the 30
bytes that sector has spare (§52.10.2). And it is unnecessary: the kernel's
contract is `BL` (§4.2).

### 4.2 The loader

FreeDOS's boot sector (fdos-kernel `boot/boot.asm`, memory map at lines
29-77) loads `KERNEL.SYS` flat at `0060:0000`, up to 128 KB, then
`mov bl, dl` / `jmp far [loadsegoff_60]` (lines 386-387). The kernel's entry
(`kernel/kernel.asm:115-124`, then `:300`) takes **`BL`** as the unit — `DL`
is never read — and reads one other thing: it probes `[SS:BP-14h]` for a
`"CL"` signature and, on a hit, ingests 255 bytes from `BP-114h` as a kernel
command line (`kernel/kernel.asm:194-232`). The stock sector leaves
`BP = 7C00h` pointing into its own stack area, where no such signature is.
`main.c:95-103` then maps `drv >= 0x80` to `3` — **C:, unconditionally** —
stashes the raw unit at `0000:05E0`, and `init_setdrive`, `FDCONFIG.SYS` and
`COMSPEC` follow that letter. The boot sector's own work area is `1FE0:xxxx`
(linear `0x1FE00..0x27E00`) and its stack `1FE0:7BA0`; the kernel runs on
that stack until it makes its own. Nothing at `0000:7C00` is consulted: the
kernel image overwrites it on any load over 0x7600 bytes, and the report's
grep for `7c00` in the kernel finds only the unrelated "boot the hard disk
instead" feature. So the **whole contract** is: the flat image at
`0060:0000`, `BL = DL = unit`, **`BP` below 114h** (zero), `DF = 0`, `IF = 1`,
and a stack with a few hundred bytes free.

So os8088 does what the boot sector does, split across the teardown:

1. **Before the teardown** (in `HIBER.DRV`, step 1a of §3.2): read
   `DOS\KERNEL.SYS` by name into a heap claim — `mem_claim_hi`, so it is
   high, above the 128 KB the kernel may occupy from `0x600` and above
   `0x27E00`; refuse a file over 128 KB or an absent one, with the reason,
   while there is still a desktop. `OSAPI_FILE_READ` by name is the whole
   read, and the bytes stay where they are.
2. **After the teardown**, the stub's **mode 2** (~40 bytes more of
   `ui_hstub`, the same page): `cli`; `DS:SI` = the claim, `ES:DI` =
   `0060:0000`, `CX` = the byte count in words, `rep movsw` (the two never
   overlap, and the copy runs upward from a source that is entirely above
   the destination); `SS:SP = 1FE0:7BA0`; `xor bp, bp`; `BL = DL = 80h`;
   `jmp 0060:0000`.
   The diskette parameter table goes to `DPT_AT` first exactly as on the
   floppy path (FreeDOS will use the floppies later).

No boot sector is read, no `AA55` checked, no `0x7C00`. The mode byte is a
second parameter in the page beside `HANDOFF_UNIT`.

### 4.3 What FreeDOS does next, and the way back

`initdisk` reads the MBR of unit 80h, assigns C: to the type-04h active
partition (drivers/hdd/hddabi.inc:96, `HPT_FAT16S`, is what every partition
this driver makes is; FreeDOS's `initdisk.c` lists 01h/04h/06h/0Eh), mounts
the FAT16 volume, finds `C:\FDCONFIG.SYS`, runs `C:\DOS\COMMAND.COM /P`,
which runs `C:\AUTOEXEC.BAT`. Every os8088 file is visible: the drivers and
`KERNEL.SYS` as hidden system files, `HIBERNAT.SYS` likewise, the packages
and documents as ordinary files. `DIR /A` shows the lot.

`OS8088.COM` is unchanged: `1234h`, `FFFF:0000`, POST, and the ROM boots
**whatever is first in its order**. With A: empty that is C:, the fresh
kernel runs §3.3 and the desktop returns with every window. With the os8088
floppy still in A: the floppy boots instead and the image is not consulted
(§3.4's last row) — the machine works, it just did not resume, and the
README says so in one sentence. Wave 3 may lift it.

### 4.4 Putting FreeDOS on C:

Three ways, all landing the same five files:

- **The build** (`build/dos-hdd.img`, §8) — done.
- **The live media**: `LIVEARGS` gains the `DOS:` payload and the two root
  files **when `build/dos/KERNEL.SYS` exists** (a `$(wildcard)`, the way
  the fetches gate `allapps`), and `--hibernate 640`. A stick that boots
  os8088, sleeps, runs FreeDOS and comes back is the release note.
- **The installer** (`hd_inst_sys`, §52.10.4): the *Copy Apps* phase learns
  a third source, the DOS floppy in B: if one is there, copying its root
  into `C:\DOS` and the two `dos/hd` editions — which therefore ship on the
  DOS floppy as `HD\FDCONFIG.SYS` and `HD\AUTOEXEC.BAT`. A machine
  installed from floppies gets FreeDOS by swapping one more disk.

### 4.5 The FREEDOS window

Two buttons, and the facts that grey them (`OSAPI_HIBER` AL = 0):

```
FreeDOS takes over the machine.
[Start FreeDOS]            - ends the session, as today; C:\DOS if present,
                             else the floppy in B:
[Sleep, then start FreeDOS] - greyed unless bit 0 & bit 1; the session comes
                             back when you type OS8088
```

`OSAPI_BOOT_UNIT` with `AL = 80h`, `AH = 1`. The warning text says which of
the two the machine can do and why the other is grey.

### 4.6 Tests

- **`dosround`** (QEMU, soak): boot `dos-hdd.img` from C: with the DOS
  floppy absent; open Calc; *Sleep, then start FreeDOS*; wait for `C:\>`
  (`tools/qmptext.py`); `DIR C:\DOS` and assert `OS8088.COM` in the text;
  type `OS8088`; assert the desktop and Calc's position. One row, the whole
  feature.
- **`dosdir`** (QEMU, soak): from the DOS prompt, `COPY CON C:\HELLO.TXT`,
  return, and assert os8088's Disk window lists `HELLO.TXT` without a
  remount — §3.3 step 2 is the thing under test.
- **`xt-dos-hdd`** on 86Box for the eye, and `os8088_xt_hdd` on MartyPC for
  the numbers.

---

## 5. Wave 3 — the field, and what was deferred

- **A real XT-IDE 8088** (docs/FIELD-MACHINES.md): the write time in §3.2 is
  an estimate; the machine gives the number, and PERFORMANCE.md Part 5 gets
  two rows: *hibernate* (sectors written, seconds) and *resume* (sectors
  read + one `wm_paint_all`, which is a boot's first paint and is priced
  there already).
- **The XMS pool**, when something allocates from it: a second section in
  the image, read and written through `xm_x87`/unreal mode by the module
  *before* the stub runs (the kernel is still whole then) — the stub never
  touches memory above 1 MB.
- **A resume from a floppy boot**: `ovl_hib_check` learns to read unit
  80h's MBR and find the active partition without `HDD.DRV` — a hundred
  bytes of overlay and a `dsk_boot_from`-shaped adoption. Deferred because
  the case is "the user left the os8088 floppy in A:" and the fix is to take
  it out.
- **Per-driver suspend/resume verbs** instead of unload/reload — only if a
  driver appears whose state is worth keeping across a sleep (a TCP session
  is not; the RAM disk's contents would be, and §62.9 already restores an
  image at boot).

---

## 6. Judged alternatives

| alternative | why not |
|---|---|
| restore in the boot sector (`boot/boothd.asm`) before the kernel loads | 478 of 508 bytes used; the two range checks that went in last took twelve of the thirty. A second loader in the reserved area is a second copy of `read_run` |
| a `.text`-resident writer | the segment has 1,988 bytes; a module costs it nothing and runs once a session — §2.8's test ("is the system disk already required?") is met, the boot volume is the hard disk |
| the stub's stack in the `0x500` page | 120 spare bytes against every ROM in the field notes; the skip sector costs 15 bytes of stub |
| reserve the top 1 KB of RAM for the stub and its stack | a permanent 1 KB off every machine, a ladder change, and `mem_claim_hi`'s first claim moves — for a stack that is used for six seconds a day |
| a "next boot" flag consumed by the loader, and a POST between os8088 and DOS | the floppy path already jumps directly, and a second mechanism for the same handover is the copy that goes stale; POST costs 2 s on an XT and buys nothing the teardown does not |
| FreeDOS on its own partition with `SYS` | §4.1: same hazards, more FATs, more code, and D: |
| snapshot XMS now | no consumer exists (kernel/xmem.inc:27); the section is designed and not written |
| suspend/resume verbs per driver | unload/reload *is* the reboot path, already correct for every driver; verbs are a per-driver contract nobody has asked for |
| hibernate from a package (a `.O88` writing the image) | no raw-sector door exists for packages (report §1.2) and adding one is a bigger contract than the module; a package cannot run after `sched_unhook` |
| a checksum over the whole image | 640 KB of `add` on an 8088 is under a second, but a corrupt body with a good header is a hazard the disk's own CRC already covers; the header sum plus the state byte cover the two failures that actually happen (a torn sector, a torn write) |

---

## 7. Which emulator answers what

| question | QEMU | MartyPC | 86Box | field |
|---|---|---|---|---|
| does the image write and restore correctly | yes (`hiber`, `dosround`) | yes | eye only | — |
| does rung 0 through a period ROM (Xebec, XUB) read the run | no (SeaBIOS) | yes (XUB) | **yes**, both machines | yes |
| the XT's write time | useless | **yes**, cycle-exact | no | **yes** |
| does the XT BIOS report two floppies (the `*-dos` question) | never (always two) | ? | **yes** | yes |
| a resume after a *cold* start | yes, by restarting the process | yes | eye | yes |
| FreeDOS's own behaviour on the shared volume | yes | yes | yes | yes |

MartyPC's hard-disk machines exist and are XT-IDE (`os8088_xt_hdd`), so the
cycle-exact rows need no new machine, only the VHD carrying `DOS\` and
`HIBERNAT.SYS` — `tools/os88hdd.py` gains `--file` for both.

---

## 8. The 86Box machines — built in this session

Two machines and one image, all in the tree now:

- **`vm/xt-dos-hdd/86box.cfg`** — the `xt-dos` XT (`ibmxt86`, 8088 at
  4.77 MHz, 640 KB, OTI-067 VGA, serial mouse) plus **`hdc_1 = xtide`**,
  the XTIDE Universal BIOS for the XT, and a drive of **63 sectors, 16
  heads, 65 cylinders** on IDE channel 0:0 backed by `build/dos-hdd.img`.
  A: is the 360 KB system disk, B: the 360 KB DOS disk.
- **`vm/386-dos-hdd/86box.cfg`** — the `386-dos` machine (`micronics386`,
  386DX/25, 2 MB) plus **`hdc_1 = xtide_at`** and the same drive. A: and B:
  are the 1.44 MB disks.
- **`build/dos-hdd.img`** (`make dos-hdd`) — §52.10's installed machine made
  by the build, with the same `tools/os88disk.py --hdd` call that makes the
  live image (§80.1), carrying the system disk's payload and §4.1's DOS
  layout. **Its geometry is the live image's, and the two `hdd_01_parameters`
  lines say the same three numbers on purpose**: `boot/boothd.asm` reads its
  geometry from the BPB, and an emulated drive told a different shape reads
  the kernel from sectors nothing wrote (docs/FIELD-NOTES.md 33).
- **`make xt-dos-hdd` / `make 386-dos-hdd`** boot them with the os8088
  floppy in A:, which is what the ROM boots first; **`HDBOOT=1`** empties A:
  so the ROM boots C: and the desktop comes up from the hard disk with the
  DOS floppy still in B:. Both nvr directories are gitignored.

**How the keys were established**: docs/TESTING.md's method — a throwaway
copy of each config launched, terminated, and read back. 86Box 6.0 (build
9001) rewrote `hdc = xtide` as **`hdc_1 = xtide`**, kept
`hdd_01_parameters = 63, 16, 65, 0, ide` and `hdd_01_ide_channel = 0:0`
unchanged, and added `hdd_01_speed = 1997_5400rpm` of its own. Both are in
the configs as 86Box wrote them, so the next launch rewrites nothing.

**What was verified on the glass** (screen captures of the 86Box window
after a timed launch): the XT with the XTIDE ROM installed and a blank disk
boots os8088 from the floppy through the loading screen; and the XT with
**no floppy** and a `--hdd` image of this geometry **boots to the os8088
desktop from the hard disk** under the XTIDE ROM — the first time any 86Box
machine in this tree has done that — and `make xt-dos-hdd HDBOOT=1` itself
reaches the same desktop. The 386 accepted the same disk and its XUB asks the
CMOS nothing about it, but on the first boot of a fresh `nvr/` it stops in
the AMI setup screen as every AT-class machine here does (EXIT FOR BOOT
once; CLAUDE.md's `RESET=` note), and it was not driven past that screen.

**What was not verified**: the FreeDOS handover on these machines end to
end, because that needs the DOS floppy — built here from the FreeDOS
payload compiled in the sibling worktree, since `make dos` cannot fetch on
this machine (§2.1) — and a person at the window: 86Box has no automation
socket, which is the reason every row in §2.2 and §3.8 is a QEMU row.

**Using them today, before wave 1**, in order:

1. `make dos` (once; needs the fetch, or the payload from §2.1's fix),
   then `make xt-dos-hdd`. A: boots.
2. Control Panel → Drivers → tick HDD; the Hard Drive page shows the disk
   and its `OS8088LIVE` partition; Mount gives it a desktop zone as C:.
3. Open FREEDOS off the system disk → *Start FreeDOS* → `B:\>`; `DIR C:`
   shows os8088's volume from DOS, `DOS\` included; `OS8088` warm-boots
   and A: boots again.
4. `make xt-dos-hdd HDBOOT=1`: the desktop comes up from C: with no floppy
   in A:, with B: still holding DOS — this is wave 1's machine, and after
   wave 2 the B: disk can come out too.

---

## 9. Decisions for the requester

1. **`Sleep...` in the System menu** (wave 1) or only through the FREEDOS
   window (wave 2)? The plan says the menu, because hibernation is useful
   with no DOS in the machine and the item is ~40 bytes.
2. **The file's size**: the machine's conventional memory (the plan), or a
   fixed 640 KB everywhere? Machine-sized is smaller on a 256 KB XT and is
   what the refusal can state exactly.
3. **`BOOT2_SECS` 13 → 14** for the overlay's check: ~24 ms on every boot of
   every machine, for a feature only installed machines use. The alternative
   is the check in `.cold` (resident, ~250 bytes, a rung of its own).
4. **Resume from a floppy boot** (wave 3): worth its hundred bytes, or is
   "take the floppy out" the answer?
5. **The pinned Open Watcom release**: the script's advisory hash becomes a
   hard pin on a dated tag; someone has to choose the tag once.
