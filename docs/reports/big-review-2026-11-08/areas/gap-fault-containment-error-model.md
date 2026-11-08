# Fault containment and the error model: one fatal path, one refusal shape, one error space, and hostile-input discipline

Gap review, 2026-11-08, against the tree as checked out (build/kernel.map and
build/smallk/kernel.map read for spans; no make, no emulator). Phase tags:
**now** = no ABI break, **flagday** = rides the next PKG_FMT/DRV_VER bump,
**record** = write the decision down, do not build yet. Bytes are signed and
say which section pays. Resident means `.text`/`.cold`/`.bss`; `.ovl` and
`.boot2` go away after boot; a module image (CTRL.DRV, DISKOPS.DRV, DESK.DRV)
is paid only while it is loaded.

## Scope read

- The prior fragments this gap pulls together: abi-9, abi-15 (areas/abi.md),
  histsys-2, histsys-11, histsys-14 (areas/histsys.md), engsys-20, video-18,
  gfx-1, wm-1, wm-2, wm-13, dw-1, dw-13, sysmisc-2, dr-9, isr-8, sched-9,
  loader-4 (areas/*.md and data/*.json).
- Every `div`, `idiv` and `xlatb` in `kernel/*.inc` and `kernel/kernel.asm`
  (103 instruction sites: 95 `div`, 1 `idiv`, 7 `xlatb`; 13 of them in
  kern_mega-only or knob-only files), with the lines before each read to find
  the fence. `drivers/` has another 48 `div`/`idiv` sites, counted, not read.
- The fatal paths: `sch_stkdie`/`sch_diepanel` (kernel/sched.inc:2566-2773),
  kern_mega's `mega_fault`/`mega_report`/`mega_fkill` and its catch frames
  (kernel/mega.inc:2405-2560, 3023-3100, 14796), `BOOT_MARK`'s exception
  stubs `mark_hook`/`mark_faulted` (kernel/kernel.asm:7647-7765).
- The error spaces: `FERR_*` (kernel/diskw.inc:57-70, apps/os88api.inc:3999-4019),
  `fm_errtab`/`osapi_ferr_text` (kernel/files.inc:2385-2420), `DRVE_*`
  (kernel/driver.inc:281-327, 2104-2125; drivers/os88drv.inc:226-260), `LD_*`
  (kernel/loader.inc:44-56), `FSKE_*` (apps/os88api.inc:2603-2611), `SPK_E_*`,
  the stream verbs' AX=4, `OSAPI_XREFUSE` (kernel/kernel.asm:2594-2605),
  `toast_say` (kernel/toast.inc:309-330), `cmz_jsayx` (kernel/compress.inc:3416).
- The hostile-input readers: the BPB table (kernel/disk.inc:5025-5300),
  `disk_read`'s CHS step (disk.inc:1745-1790), `dsk_next_clus_x` and the
  directory walk cap (disk.inc:6537-6600), the settings unpack and `CFG_MAP`
  (kernel/driver.inc:880-960, 4955-4975), the dock byte (dock.inc:300-316,
  dockmod.inc:280-296), fsck's cluster arithmetic (fsck.inc:1815-1830, 2895-2920).
- The keyboard path for a bracket escape: `kbm_isr` and its ROM chain
  (kernel/mouse.inc:725-760, 2875-2960), SPEC.md 53.1.
- The DOS layer's IVT exchange (apps/dos/dosw.inc:75-130), because a kernel
  int 0 handler has to coexist with it.

## Verdict

The kernel already has **one excellent instance of each of the four things
the brief asks for**, and none of them is applied as a rule:

- **one fatal path**: the stack-overflow panel is small (174 bytes on
  kern_big), safe to draw (SP moves first, no lock, `font_run`), and names the
  culprit. It is wired to exactly one cause. Divide overflow, the cause
  that the review's top findings keep producing, goes to the BIOS dummy
  vector. On the owner's 5150 that stub masks every IRQ (the tree's own
  account, wm.inc:8280-8292). On every 286+ it re-executes the `div` for
  ever. kern_mega already contains the full model: a fault in a package's
  own code closes the package, and a kernel fault is a panel.
- **one refusal shape**: `OSAPI_XREFUSE` answers AX=1, CF=1, and `toast_say`
  bounds every table it letters. Elsewhere CF is the only common part of a
  refusal (abi-9).
- **one error space**: `FERR_*` has a published text door. Four other code
  families do not, and each has its own string table.
- **hostile-input discipline**: the BPB's 17-rule table is the reference
  implementation, with a per-call quotient guard at every geometry divide.
  The rule stops there. Directory entries (dw-1), `SYSTEM.CFG` bytes
  (sysmisc-2), driver strings (dr-9) and package pointers (wm-1, loader-4,
  histsys-11, gfx-1) are fenced site by site or not at all.

The cheap systemic move is to **make the divide fault a named stop on every
kernel (~+40 resident)** and then **make the fence a ratchet rather than a
habit (0 bytes)**. The ratchet is a registry of every divide site with its
proof, on the `tests/textsites.txt` precedent. Together they turn six
present findings from "hang, corruption or silent continue" into a
diagnosis, and stop the next one landing unproven. A unified error NUMBER
space is not worth a flag day. A unified error CLASS is, and it costs
nothing: DOS 3's class and action columns, written into the SDK.

## Strengths

1. **The BPB table plus per-call quotient guards** (disk.inc:5025-5300,
   1755-1767). Each rule names the downstream consumer it protects, including
   "a divide fault with `sch_lock` held" (SPEC.md 34395-34410). The divide in
   `disk_read` still refuses a quotient that will not fit
   (`cmp dx, cx / jae .jfail`, disk.inc:1757-1758), even after mount
   validation. That is defence in depth at 4 bytes.
2. **`fsk_needcl` saturates rather than faults** on a hostile FileSize
   (`cmp dx, cx / jae .sat`, fsck.inc:1824-1826). This is exactly the guard
   dw-1 says the write layer lacks. The fix is already in the tree, in the
   module that repairs disks.
3. **Table dispatch is bounded everywhere it was read.** `ui_cmd`'s
   wrap-and-compare (ui.inc:3022-3028), `clk_rtc_write`'s "belt as well as
   braces" (clockw.inc:71-76), `toast_say`'s unsigned bound (toast.inc:322-330),
   and `osapi_ferr_text`'s "a hostile code above 0x7FFF is the same unknown
   code as 11" (files.inc:2410-2416). Every `xlatb` on a colour is preceded by
   `and al, 15` (font.inc:994, 1339, 1349, 2434).
4. **Future-byte handling for settings**: the dock's edge byte clamps "3: a
   byte from a future version" (dockmod.inc:292-295), and `thm_set` owns its
   own refusals (driver.inc:4976-4985).
5. **The death panel's construction** (sched.inc:2635-2724): off the dead
   stack first, the clip disarmed, no lock taken. It costs 174 bytes of
   `.text` and 4 of `.bss` on kern_big (`sch_stkdie` 53AE to `sch_stkbase`
   545C) and 173 on kern_small. It is the shared body any other fatal cause
   should use.
6. **kern_mega's fault containment** (mega.inc:2504-2560, 3023-3100). A
   package's fault unwinds to the innermost catch pushed by `wm_pkgcall` and
   the loader, so the call returns CF=1 as if the callback had refused. A
   worker with no catch is killed, and a kernel fault stays fatal. It also
   has loop protection: `MEGA_FKCAS` and `MEGA_FKGAP`. This is OS/2 1.x's
   model, done properly.
7. **The DOS layer banks IVT 00h per world** (dosw.inc:89-110, "IVT 00h
   (divide - a C runtime hooks it)"). A kernel int 0 handler is therefore
   already the "desktop's copy" that `dvw_copy` restores. Installing one
   needs no change to DOS.
8. **`BOOT_MARK` already wrote the stub shape**: 4 bytes a vector,
   `mov al, n|80h / jmp short mark_faulted` (kernel.asm:7659-7671). It also
   records the right exclusions: not NMI, not 8 and up.

## Findings

### gfce-1 (hazard, HIGH, now) - one fatal path: divide fault (and #UD on a 286+) into the death panel

**Evidence.** Nothing writes IVT 0 on kern_big or kern_small outside
`BOOT_MARK` (kernel.asm:6093, 7725-7758). The tree states what that costs in
three places: instance.inc:1975-1977, wm.inc:8280-8292 ("On an IBM 5150/5160
ROM that vector is the BIOS's own stub, which writes 0FFh to the 8259's mask
and IRETs: every interrupt in the machine is gone") and ctrl.inc:5966-5975.
The last is a SHIPPED bug: a click in the Control Panel's sound-page gutter
gave a negative x, an unsigned test passed it, and the quotient overflowed:
"INT 0, and no handler for it". So the path is reachable from ordinary input
as well as from hostile data.

**What it converts.** Each of these becomes "a named stop" instead of a
silent mask-all on a 5150, a spin on a 286+, or garbage on other 8088 ROMs:

| finding | today | with the panel |
|---|---|---|
| wm-1 / loader-4 (`wm_ptr2idx` on a wild BX) | 5150: all IRQs masked, motor on; 286: spin | `DIVIDE ERROR TASK 00 AT xxxx:yyyy UI TASK` |
| dw-1 (FileSize >= 32MB, APPEND / READ_AT) | spin or garbage inside the file layer | named stop (the guard is still owed) |
| gfce-4 (FSX_SPK N=0, IF=0) | 286: spin with interrupts off, a dead machine | named stop |
| deskmod `.make` (gfce-5) | as wm-1 | named stop |
| every package and C package `/` on file data | as above, blamed on the kernel | names the PACKAGE, by I_NAME and CS |
| a wild jump into data on a 286 (#UD) | BIOS dummy, re-executes for ever | `INVALID OPCODE ...` |

**Design (both kernels).** Install from the boot overlay (`.ovl`/`.boot2`,
which goes away): vector 0 always, vector 6 only when
`[cpu_tier] >= CPU_286`. On an 8088 vector 6 is no CPU exception, and int 5
is PrtSc on a PC, so never that one. The stub is resident `.text` in
KERNEL_SEG:

```
sch_f0: cli              ; 8086: [SP] = IP AFTER the div; 286+: AT it
        pop ax           ; AX = IP
        pop dx           ; DX = CS
        push cs
        pop ds
        mov si, sch_s_div
        jmp short sch_diepanel.fault
```

`sch_diepanel` grows one entry point. Its shape stays as it is, except that
the message is passed in SI (sch_stkdie's `mov si, sch_s_over` moves above
the fall-in, 0 net). The `'  SP '` label becomes a generic `'  AT '`. The
fault path prints DX:AX where the overflow prints `T_SP`. sched-9's bound on
the instance index (`cmp al, INST_MAX / jae .name`, +4) rides with it,
because a divide fault is the case where the table really is trustworthy and
the name is the whole report.

**Bytes.** About 11 for each stub (two stubs, the second loading a different
SI), 13 for `'DIVIDE ERROR'`, 15 for `'INVALID OPCODE'` (kern_big only), 6
for the CS print, 4 for the sched-9 bound, and ~6 for the entry and label
rework. That is **about +40 resident on kern_small and +55 on kern_big**,
ESTIMATED. Add +12 `.ovl` for vector 0 and +8 more for the gated vector 6.
Without vector 6: ~+40 on both.

**The policy cost, said plainly.** On a non-IBM 8088 ROM (GLaBIOS, the 1986
XT BIOS and most clones) a divide overflow currently *continues*, often
harmlessly. The panel turns that into a stop. That matches the kernel's
written doctrine ("A halted machine is a bug report; a machine that carries
on is a bug that gets blamed on something else three subsystems away",
sched.inc:2580-2583). It also means a latent package defect that has never
been noticed will start stopping the machine. gfce-2 is the answer if that is
too blunt.

**Suite.** A soak row: a tests/ package that divides by zero from a window
callback. It is asserted on MartyPC's 5150-GLaBIOS (panel string read back
through `sch_dpcol`'s advance, or `shot`) and as `box-<row>` on an 86Box 286,
where today it spins. Break it on purpose by dropping the install: the row
must time out.

Listed as: not in ABI-BREAK-LIST or KERNEL-BYTE-QUEUE. It is the same
proposal as isr-8 and histsys-2, scoped here to both vectors and priced
against today's panel span.

### gfce-2 (rethink, MEDIUM, record) - a package fault should close the package, not the machine

UNIX delivers SIGFPE to the process and panics only for a kernel-mode fault.
DOS's own int 0 handler prints "Divide overflow" and terminates the program.
Windows 3.0's UAE and OS/2's trap screen kill the application. All four
contain a fault to the process that raised it. kern_mega does the same
(`mega_fkill`, mega.inc:2504-2539), but needs the catch frames pushed around
every package call (`mega_pcin_x`, mega.inc:3038-3075: 10 bytes of stack
and ~40 instructions per callback) to unwind a UI-task callback.

A real-mode version can be had without catches **for the fault that is the
package's own instruction**. Find the owning instance from CS:
`CS == [I_SEG]` of a live record, or inside a claim it owns. If there is one
and it is not a kernel CS (KERNEL_SEG, COLD_SEG, a module's claim), then:

- request its close (I_STATE 2, the path the Close menu uses);
- queue a toast `'<NAME>: divide error'`;
- set AX = FFFFh;
- IRET past the instruction.

On an 8088 the pushed IP is already past it. On a 286+ the stub must step
over the `div`/`idiv`: F6/F7 plus ModRM, with 0, 1 or 2 displacement bytes
and a possible segment prefix. That decoder is about 20 bytes. The package
runs on until its close is processed. That is the DOS bargain, and it keeps
the machine. A kernel CS, a worker inside `sch_lock`, or the rate hook (the
`[sch_rbusy]` test that `mega_fkill` already makes) still goes to the
gfce-1 panel.

**Bytes.** About +45 to +65 resident (instance scan ~20, 286 skip ~20,
toast and close ~15), ESTIMATED. This is worth deciding before gfce-1 ships,
because it decides what the panel is FOR. **Record**: it is an owner call
between "a halt is a bug report" and "the app dies, the desk lives".

### gfce-3 (weakness, MEDIUM, now, 0 bytes) - the divide fence is a habit; make it a ratchet

**Evidence.** I classified the 90 kernel `div`/`idiv` sites outside
kern_mega and knob files by where the operands come from (approximate
counts, by reading):

| class | sites (≈) | fence | unfenced |
|---|---|---|---|
| constant divisor, kernel-bounded dividend (digits, clock, percent, `aam`) | 40 | by construction, usually proved in the comment (compress.inc:3436-3441 is the model) | none found |
| mouse/pane coordinate hit tests (`div r8` by a row pitch) | 20 | a SIGNED guard before, the screen size after: implicit | none today. ctrl.inc:5975 was one until it shipped broken |
| disk geometry and BPB-derived | 20 | BPB rules 4/11/12/13 plus per-call quotient guards | **diskw.inc:3170, 3932** (dw-1) |
| a pointer into a kernel table (`wm_ptr2idx`, `inst_wchk`, deskmod `.make`, `fmv_owner`, `mem_val`) | 5 | `inst_wchk` only (instance.inc:1984-1986) | **wm.inc:7283** (wm-1), **deskmod.inc:254** (gfce-5) |
| a caller's number | 3 | `snd.inc:180` (freq >= 19) | **snd.inc:1117, fsx.inc:4008** (gfce-4) |

So 5 of ~90 are unfenced, and every one of them is a kernel routine trusting
a value that crossed a boundary. The fenced ones are fenced by a comment
somebody wrote correctly. The project already has the mechanism for "a
property every call site must argue": `tests/textsites.txt` with its fast
gate, and `tests/toastdyn.txt`.

**Recommendation.** Add `tests/unit/t_divsites.py` (fast) and
`tests/divsites.txt`. Every `div`/`idiv` in `kernel/` and `drivers/` is
registered by file and label (not by line) with one of four reasons:

- `const` (divisor constant, dividend bound named);
- `fenced <label>` (the guard instruction's label);
- `validated <rule>` (BPB rule n, `CFG` clamp);
- `kernel-only` (both operands kernel state).

A new divide fails the build until it is registered, and the count of
`unfenced` lines only goes down. The gate does not prove the reason. It
forces somebody to write one at the moment of writing the divide, which is
when ctrl.inc:5975's author would have seen it. 0 shipped bytes. The same
gate can carry `xlat` and computed `jmp [..+bx]`.

Listed as: none. It complements dw-1, wm-1 and loader-4 rather than
replacing them.

### gfce-4 (bug, LOW, now) - `OSAPI_FSX_SPK` divides by the caller's N with interrupts off

`osapi_fsx_spk` (snd.inc:1082, kern_big only) documents "DX = N ... not
checked here: packages are trusted" (snd.inc:1073-1074). It then does
`mov bx, dx / xor dx, dx / div bx` (snd.inc:1104-1117) under the contract's
IF = 0. kern_mega's real-mode twin `rms_spk` does the same (fsx.inc:3977,
4006-4008). N = 0 is a divide by zero. On a 286+ it spins at IF = 0 with the
PIT already reprogrammed, which is as dead as a machine gets. On an 8088 it
continues with K = garbage and trims `[sch_rdiv]` by a garbage remainder,
which moves the system clock. Any N >= 1 is safe (the dividend is under
65536). **Fix**: `test dx, dx / jz` to a new `SPK_E_ARG` = 2. The number
is free between SPK_E_NOTRATE 1 and SPK_E_BUSY 3 (apps/os88api.inc:797-799).
**Bytes**: +4 `.text` kern_big, +4 kern_mega's `rms` part, 0 kern_small (no
door). The SDK gains a code: an ABI clarification, so `abilock.py --update`
without a bump.

### gfce-5 (hazard, LOW, now) - the desk service's `.make` divides a package BX fenced by plausibility only

`DESK.DRV`'s `.make` arm (deskmod.inc:236-258) reads `[bx+W_FLAGS]` and
`[bx+W_SEG]` and then does `div cl` on `bx - wm_wins` with no range test.
The fence is `[bx+W_SEG] == [ld_base]` during a load, which a wild BX passes
only by coincidence. It is the wm-1 shape in a module. **Fix**: take wm-2's
stored index if that lands. Otherwise the three-instruction `wm_isrec`
bound first (+5 bytes of DESK.DRV, 0 resident).

### gfce-6 (weakness, MEDIUM, now, 0 resident) - `SYSTEM.CFG` gets a domain column, not a per-byte patch

The unpack copies every mapped byte raw (`%%up_u`, driver.inc:4965-4972).
CLAUDE.md's "every byte read off one is still treated as hostile" is then
honoured by each *reader*. The dock clamps, `thm_set` refuses, and
`clk_h12` is used as a count by the Date/Time page and corrupts `.bss`
(sysmisc-2). Of the 8 (kern_small) or 15 (kern_big) mapped bytes, the domains
are tiny: 0..1 for `CH`/`CS`/`ANIM`, a tier range for `SR`, bitmasks for
`SSM` and `DOCK`, and 0..15 for the five `THMC` colours.

**Recommendation.** Give each `CFG_MAP` row a fourth byte, its MAX. The
unpack becomes `cmp al, [cs:si-1] / jbe .ok / xor al, al`: an out-of-domain
byte reads as the default 0, which is the documented outcome of a key that
does not match ("old records stop matching and the default stands",
driver.inc:883-885). sysmisc-2's `and byte [clk_h12], 1` is then one row's
MAX rather than a special case, and the next count-shaped setting is
covered on the day it is added. **Bytes**: +15 (kern_big rows) and +7 code
in the boot overlay, and the same again in CTRL.DRV (the macro is emitted
into both images, driver.inc:887-893). **0 resident**.

### gfce-7 (consistency, LOW, now, 0 bytes) - `cmz_jsayx` tells a code from a string at 16, but the table has 11

`cmz_jsayx` treats AX < 16 as a FERR_* and indexes `fm_errtab`
(compress.inc:3416-3429). `fm_errtab` has `FM_NERR` = 11 entries
(files.inc:2396-2399). A code 11..15 would letter the first words of
`osapi_ferr_text`'s code as string pointers. Only the kernel's own calls
feed it, so it is latent, but it is `toast_say`'s documented failure ("an
out-of-range code here would letter whatever the word after the table
happens to be", toast.inc:325-327). **Fix**: `cmp ax, FM_NERR`. The same
length, and it keeps the discriminator in step with the table.

### gfce-8 (weakness, MEDIUM, now 0 / flagday ~0) - one error CLASS, not one error NUMBER

**The spaces as they stand** (code: meaning):

| code | FERR_* | DRVE_* | LD_* | FSKE_* |
|---|---|---|---|---|
| 1 | NODISK | DISK (no system disk) | EDISK | NOTHERE |
| 2 | IO | NOENT | EBAD | IMAGE |
| 3 | NAME | BAD | EBIG | NOFAT |
| 4 | NOENT | MEM | EABORT | IO |
| 5 | EXIST | HW | ENOMEM | CHANGED |
| 6 | FULL | BUSY | EVER | SMALL |
| 7 | DIRFULL | TWICE | - | WPROT |
| 8 | PROT | NOVOL | - | ARG |
| 9 | WPROT | CLASS (big) | - | NOENT |
| 10 | BIG | - | - | - |

Then there are the verb-local codes: SPK_E 1/3, the stream verbs' AX=4, the
sound verb's 7, XREFUSE's 1, `DRV_CALL`'s 0, and the bare `stc` stubs that
leave AX untouched (snd.inc:710-713 serves three cells that way). The same
number means four unrelated things, but **no consumer ever mixes families**.
The file manager letters FERR and LD, the Drivers page letters DRVE, fsck
letters FSKE. So renumbering into one space buys a single text door and
nothing else. Each family's strings are already the one copy, and a
package only ever holds FERR codes.

**What is missing is the column a program acts on.** DOS 3.0's int 21h
AH=59h answered a CLASS (out of resource, not found, media, hardware,
already exists, bad format, locked, application error, internal) and an
ACTION (retry, delay-retry, re-enter input, abort, user intervention). The
mapping onto those is total, and short:

- **media / user action**: FERR_NODISK, WPROT; DRVE_DISK; LD_EDISK;
  FSKE_WPROT, CHANGED
- **hardware / retry**: FERR_IO; DRVE_HW; FSKE_IO
- **not found**: FERR_NOENT; DRVE_NOENT, NOVOL; FSKE_NOENT, NOTHERE
- **already exists**: FERR_EXIST
- **out of resource**: FERR_FULL, DIRFULL, BIG; DRVE_MEM; LD_EBIG, ENOMEM;
  FSKE_SMALL
- **bad format**: DRVE_BAD; LD_EBAD, EVER; FSKE_IMAGE, NOFAT
- **locked / in use**: FERR_PROT; DRVE_BUSY, CLASS
- **application error (re-enter input)**: FERR_NAME; LD_EABORT; FSKE_ARG
- **internal (a bug)**: DRVE_TWICE

**Now (0 bytes).** Write that table into SPEC.md 18.4 beside the FERR list.
Put one sentence in the SDK preamble: "CF=1 is the only universal refusal.
AX is a code only where the cell names its family, and AX=1 always means
'not on this kernel' in a cell that names XREFUSE." That is abi-9's "now",
and it settles which column a new code goes in. **Flag day (~0).** Take
abi-9's AX=1 "no such service here" across every family. XREFUSE,
FERR_NODISK and FSKE_NOTHERE are already 1, so the moves are `DRV_CALL`'s
`xor ax, ax` (driver.inc:6662) and each bare `stc` stub (+3 a stub that
keeps its own body, 0 where it can share XREFUSE's). **Not** a renumbered
single space: its only buyer is one door, and the price is every
FERR-checking package rebuilt. Listed as: abi-9, histsys-14. Neither is a
row in ABI-BREAK-LIST, and abi-9's flag-day half belongs there as a row.

### gfce-9 (prior-art, LOW, now, 0 shipped) - `APICHK=1`: the checked kernel, concretely

abi-15 and engsys-20 propose it. What makes it cheap here is that **cell
length is already ABI and macros already build every cell**
(kernel.asm:2432-2530):

- In an APICHK build, `OSAPI_SLOT %1` and `OSAPI_RCELL kind, %1` emit
  `call apichk_%1` / `dw apichk_%1` whenever a host-generated
  `build/apichk.inc` defines `APICHK_<name>`. The cell is the same length.
- The generator reads the SDK's own contract comments. "BX = a window of
  YOURS" becomes a W check, "UI task only" a T check, any `gfx_*` draw a G
  check, and every cell a D check.
- Each `apichk_` stub runs its checks and jumps to the body:
  - **W**: `wm_isrec`, plus `W_SEG == caller DS` (the inst_wchk test);
  - **T**: `[sch_cur] = 0`;
  - **G**: the gfx lock is held by `[sch_cur]`, and widths and heights are
    signed-positive (gfx-1's class);
  - **D**: DF = 0.
- A violation goes to port E9 and the debug registry (§57) with the cell
  number and the caller's CS:IP, then the call proceeds, Windows 3.1 debug
  KERNEL style ("RIP and continue").

It would have reported wm-1 and loader-4's BX, histsys-11's stale wake
pointer, gfx-1's negative width and wm-4's clobbered BX at the first
offending call, against the package that made it. **Row**: the shape of
`smalllaunch` (tests/suite.py:12288): open every package on the system and
apps disks under APICHK, and fail on any E9 `APICHK` line. Soak tier, one
package sweep. 0 shipped bytes. Guard 2 is the only bound on the knob
kernel, and `soak -k buildmatrix` gains one arm.

### gfce-10 (consistency, LOW, record) - kern_mega has two fatal panels with two vocabularies

`STACK OVERFLOW  TASK 04  SP 1A2C  TASKMGR` (sched.inc:2765) and
` kern_mega FAULT vv err eeee at cccc:iiii sp ssss - reboot `
(mega.inc:14796), each with its own hex routine (`sch_dphex4`,
`mega_bhex4`). Once gfce-1 exists, all three causes can print one line,
`<WHAT> TASK nn AT cccc:iiii <NAME>`, through `sch_dpstr`. kern_mega adds
its error code. **Bytes**: roughly -20 `.mega` when `mega_bhex*` and the
buffer staging go, ESTIMATED. Record, to do with gfce-1.

### gfce-11 (weakness, LOW, record) - the hung fsx bracket already has an exit, and it is Ctrl-Alt-Del

video-18 prices a Ctrl+Alt+Esc latch (+15). The keyboard path changes the
trade. `kbm_isr` always ends in a far jump to the ROM's int 09h
(mouse.inc:2954, `.chain`), and the bracket leaves int 09h alone on the
real-mode kernels. So **the BIOS warm reset already works from inside a
hung bracket**, as SPEC.md 26691 says it does from shutdown's halt. A
latch would rescue only a loop that already polls `fsx_wait` and ignores
its own exit key. That loop is a bug the app's author will find on day
one. Two cases defeat Ctrl-Alt-Del:

- an app that replaced int 09h itself;
- a divide fault on a 5150 ROM, which masks IRQ1. gfce-1 turns that one
  into a panel.

**Recommendation**: no latch. SPEC.md 53.1's sentence should say that
Ctrl-Alt-Del is the escape (see the mismatches). 0 bytes.

## Rethinks

- **Two tiers, not one (gfce-1 plus gfce-2).** Every system the brief names
  settled on the UNIX split. A fault in user code is the process's: SIGFPE,
  "Divide overflow" and terminate, UAE, an OS/2 trap that kills the process.
  A fault in system code is the system's: panic, the bomb box. os8088 has
  the second tier for one cause, and kern_mega has both tiers for every
  cause. The real-mode kernels can have both for the cause that matters
  (divide) at ~+100 resident in total, without catch frames, because on an
  8088 the CPU has already stepped past the instruction.
- **Make hostility a property of the reader, not of the call site.** The BPB
  works because ONE routine validates and every consumer then trusts fields
  that are already proven. The settings unpack (gfce-6) and directory entry
  staging (dw-1's FileSize, at `dsk_get_dir`) are the two other readers that
  could own their domain the same way. One clamp at staging replaces N
  guards at N divides.
- **Validation as a build, not as resident bytes.** Win 3.1 retail added
  validation at the USER/GDI door. Here that would cost resident bytes in
  every cell, and SPEC.md 20.8's trust model says not to pay them. The
  debug-kernel half (gfce-9) is the part worth having, and the macro
  structure makes it free.

## Prior art

- **MS-DOS**: its int 0 handler prints "Divide overflow" and terminates
  the current program, not the machine. int 24h hands the critical error
  to a handler with Abort/Retry/Ignore/Fail, which is an ACTION column.
  Int 21h AH=59h (DOS 3.0) returns extended error, class, action and
  locus, the four columns gfce-8 adopts the middle two of.
- **Macintosh System 1-7**: one `OSErr` number space across every manager,
  and one `SysError(id)` path that draws the bomb box with an ID. The single
  fatal path is the part to copy; the single number space was affordable
  because managers shared callers, which os8088's families do not.
- **Windows 3.0/3.1**: 3.0's UAE kills the faulting app. 3.1 added parameter
  validation at the API boundary (`IsWindow` on every HWND) and a debug
  KERNEL that RIPs and continues, which is what found most Win16 bugs.
- **OS/2 1.x**: a protection fault in a process kills that process with a
  register dump. A fault in the kernel is the trap screen. kern_mega's
  `mega_fkill` is this, in this tree.
- **System V / UNIX**: SIGFPE delivered to the process (default action:
  core and exit) and `panic()` for a kernel-mode fault. That is the
  two-tier split.

## Doc/code mismatches

1. **SPEC.md:13546**: "The evidence is banked into `.bss` first, on the
   stack we arrived on, and only then does `SP` move". The code moves SP
   first, then reads the evidence from the task table into registers and
   pushes it on task 0's stack (sched.inc:2686-2699). Its own header says
   ".bss ... until kernel size pass 8; the read preceded the move until
   pass 9". **Proposed**: "`SP` moves first, to task 0's stack (the largest
   in the machine and, at `IF` = 0 for ever, owned by nobody), and the
   evidence - the slot, its parked `SP`, its instance - is then read out of
   the task table into registers and pushed there. Nothing reads a task
   record after that, so it does not matter that the table is suspect; and
   if task 0 *is* the corpse, its contents are already in registers."
2. **SPEC.md:13573**: "It costs 209 bytes (`.text` +201, `.bss` +8)". The
   build says 174 `.text` on kern_big (`sch_stkdie` 53AE to `sch_stkbase`
   545C, build/kernel.map:4299-4313), 173 on kern_small
   (build/smallk/kernel.map:3050-3064), and 4 `.bss` (`sch_dpcol`,
   `sch_dphc`, sched.inc:2966-2968). **Proposed**: "It cost 209 bytes when
   it landed (`.text` +201, `.bss` +8); size passes 6 to 13 have taken it to
   174 of `.text` on kern_big (173 on kern_small, `sch_stkdie` to
   `sch_stkbase`) and 4 of `.bss`, and it is **unconditional** ...".
3. **SPEC.md:13520** and **kernel/sched.inc:2591**: "`sch_stkdie` is the
   only `cli`/`hlt` in the kernel" / "This is the only cli+hlt in the
   kernel". kern_mega's `mega_fault` does `cli` (mega.inc:2411) and ends in
   `.dead: hlt` (mega.inc:2499-2501). A BOOTMARK=1 build adds
   `mark_faulted` (kernel.asm:7718-7723). **Proposed** (SPEC): "`sch_stkdie`
   is the only `cli`/`hlt` in kern_big and kern_small (kern_mega's
   `mega_fault` is the other, and a `BOOTMARK=1` build adds
   `mark_faulted`), and until §8.8 it printed **nothing**". The same scope
   applies to the sched.inc comment: "the only cli+hlt in kern_big and
   kern_small".
4. **apps/os88api.inc:4007**: `FERR_DIRFULL ... ; the root directory is
   full`. The kernel's definition is "the current directory has no free
   slot" (diskw.inc:65), subdirectories are writable (SPEC.md 19.2), and the
   user is told 'Folder full' (files.inc:2392). **Proposed**: `; the
   directory has no free slot (the root, or a full subdirectory that cannot
   grow)`. An SDK comment edit moves the abilock fingerprint, so it needs
   `abilock.py --update` without a bump.
5. **apps/os88api.inc:4010**: `FERR_BIG ... ; larger than the buffer (or
   than 64KB)`. Counts have been 32-bit for some time. SPEC.md 34520 says
   "`FERR_BIG` for a count >= 32MB", and diskw.inc:68-70 says "or, on a
   write, than the 32MB sector count a 16-bit count can hold". **Proposed**:
   `; larger than the buffer - or, on a write, than the 32MB a volume here
   can hold (SPEC.md 18.4.1)`.
6. **kernel/instance.inc:1975-1977**: "`div cl` FAULTS ... once BX -
   wm_wins exceeds 255*WIN_SIZE". The quotient overflows AL at 256 x
   WIN_SIZE: offsets 255 x 74 = 18,870 to 18,943 still give 255. wm-1
   repeats the 255. **Proposed**: "once BX - wm_wins reaches 256*WIN_SIZE
   (unsigned, so any BX below the table too)".
7. **SPEC.md:106661-106663**: "An app that hangs in the bracket hangs the
   machine - the same truth as any runaway callback, documented rather than
   defended". `kbm_isr` chains every IRQ1 to the ROM's int 09h
   (mouse.inc:2954) and the real-mode bracket does not touch int 09h, so
   the BIOS warm reset still answers. **Proposed**: "An app that hangs in
   the bracket hangs the desktop - the same truth as any runaway callback,
   documented rather than defended. Ctrl-Alt-Del still warm-resets: IRQ1
   reaches the ROM's int 09h through `kbm_isr`'s chain, unless the app
   replaced that vector itself."

## Open questions

- **QEMU's divide fault.** wm.inc:8289-8292 says "GLaBIOS and QEMU do not
  mask, so the same defect is a wrong clip index there and invisible". QEMU
  raises #DE as a fault pointing AT the `div` on every CPU model. If so, the
  dummy IRET re-executes it and the faulting task spins: not invisible, and
  not a wrong clip index. One QEMU run of gfce-1's test package settles
  which comment is right.
- **NEC V20/V30.** Do they push the IP after the `div`, like the 8088, or
  at it? gfce-2's skip decoder must know. It is a field-machine question
  (docs/FIELD-MACHINES.md).
- **gfce-1's vector 6 on kern_big only.** kern_small boots 286s too. Is the
  #UD stub's ~+20 worth it on the 128KB build, whose machines are
  overwhelmingly 8088s?
- **The IBM 5150 ROM's dummy handler.** The tree's claim that it writes 0FFh
  to the 8259 mask matches the 1981 listing's temporary handler. The 1986
  XT BIOS's version reads the in-service register and masks only the
  in-service line, storing 0FFh in `INTR_FLAG` (40:6Bh) otherwise. If
  pc5150's ROM is the later one, a divide fault outside an ISR continues
  there as on GLaBIOS.
- **Who owns gfce-2's decision**: "a halted machine is a bug report"
  (sched.inc) against "the app dies, the desk lives" (every system in the
  prior-art list). It is the owner's call, and it decides what gfce-1's
  panel is for.

## Verification

Adversarial pass, 2026-11-08, against the current tree (the doc-fix pass had
already edited some of the quoted files). Every finding was re-read at the
cited code.

| id | verdict | note |
|---|---|---|
| gfce-1 | confirmed | Nothing writes IVT 0/1/3-7 on kern_big or kern_small outside BOOT_MARK's `mark_hook` (kernel.asm `mark_fvec` 0,1,3,4,5,6,7). The 286+ half is architectural: #DE pushes the IP *of* the `div`. The panel is 174 bytes on kern_big and 173 on kern_small (`sch_stkdie` to `sch_stkbase`, strings included), as the maps say. The 5150 masking claim rests on wm.inc's note; the open question about the 1986 XT BIOS revision stands. Not in ABI-BREAK-LIST or KERNEL-BYTE-QUEUE. |
| gfce-2 | confirmed | A decision to record. `mega_fkill` is the precedent; 8086/8088 #DE pushes the address after the `div`. |
| gfce-3 | confirmed | Counts reproduced: 95 `div`, 1 `idiv`, 7 `xlatb` in kernel/; 48 `div`/`idiv` in drivers/. One of the five unfenced sites (FSX_SPK) is unfenced *by ruling*, so a ratchet registers it as such. |
| gfce-4 | **refuted** | The divide is real, but SPEC.md 34.11.1 records the decision: N must be 74..255 and "the door does not re-check it: packages are trusted (the owner's ruling, docs/plans/LAST-DROP-BYTES.md §7.11)", a fence having cost 55 bytes. Also, on an IBM ROM 8088 the int 0 dummy masks the 8259 rather than continuing. It stays a beneficiary of gfce-1. |
| gfce-5 | confirmed | `.make` fences on W_FLAGS, `[ld_base]` == W_SEG and the record's I_STATE only, then `div cl` and an unbounded `wm_owner+si` read. |
| gfce-6 | corrected | Raw copy confirmed (`%1_unpack`; 15 rows on kern_big with DOCK_OPT, 8 otherwise). An out-of-domain byte should skip the store, so the boot default stands as the map header promises. Storing 0 would be wrong: 0 is not every key's default. |
| gfce-7 | confirmed | `cmp ax, 16` against FM_NERR = 11; latent. The imm8 encoding is the same, so the fix is 0 bytes. |
| gfce-8 | confirmed | Every code named in the class map exists; abi-9's refusal shapes are confirmed there. |
| gfce-9 | spot-checked, confirmed | Cell macros at kernel.asm:2425/2442/2495; smalllaunch at suite.py:12288. |
| gfce-10 | spot-checked, confirmed | The two vocabularies exist (sched.inc `sch_s_over`, mega.inc `mega_fbuf`). |
| gfce-11 | confirmed | Every `kbm_isr` path falls to `.chain` (KBM_SCAN only flags keypad 5). kern_big's bracket touches neither IVT 09h nor IRQ1's mask, so Ctrl-Alt-Del answers. |

**Doc mismatches.** Four are confirmed and go to the data file's `doc_fixes`:
- SPEC.md 8.8's "only `cli`/`hlt` in the kernel" is scoped to kern_big and kern_small (kern_mega's `mega_fault`, plus the BOOTMARK/BOOTHALT knobs).
- `FERR_BIG`'s "(or than 64KB)" in os88api.inc. It is an `equ` comment, so the abilock fingerprint does not move.
- instance.inc's "exceeds 255*WIN_SIZE" becomes "reaches 256*WIN_SIZE, taken unsigned". `wm_wins` is 9FCAh / 766Bh, so every BX below it faults.
- SPEC.md 53.1's "hangs the machine" becomes the desktop, with the Ctrl-Alt-Del exit.

Three are **dropped as already applied** by the doc-fix pass: SPEC.md 8.8's ".bss first" order, its "costs 209 bytes", and `FERR_DIRFULL`'s "root directory".
