# ksp12 agent "ctrl" - CTRL.DRV, the Control Panel module

Kernel size pass 12. Concept: the Control Panel module's unpassed code
(kernel/ctrl.inc, an on-demand module, SPEC.md 2.8 - RAM only while the panel
is open, disk always):

| commit | what | CTRL.DRV then |
|---|---|---:|
| `dde6b19` (#248, main) | Port pins: the Drivers page's Ports view (SPEC.md 31.15, 51.9.2) | +686 |
| `c461c5b` | Force Color (SPEC.md 39.28) - the box, and `VGC_FORCE` in the settings core | +238 |
| `c4b43d0` | Windows theme - the fourth radio and its refusal toast | +77 |
| `edd0efb` / `8cead00` | the `DA` key, `HDD.DRV` autoload | +25 / +19 |

Branch `ksp12-ctrl`, cut from `df8603a1` (the brief). Every figure is
ASSEMBLED: module images are the UNPACKED image cut by `tools/os88mod.py`
(kern_big's ships plain; kern_small's is `CZ`-wrapped, measured through
`os88drv.image_unwrap` and by cutting the image from a listing assembly),
kernels are `tools/kernsize.py --json` absolutes.

## BASE AND TIP

Base = `df8603a1` (the brief), measured on this branch's two doc commits
over it (they move no byte). Tip = this branch's last commit.

| | base | tip | delta |
|---|---:|---:|---:|
| **CTRL.DRV kern_big** (shipped plain) | 12,346 | **12,113** | **-233** |
| ...its claim (image + 152 bytes of `.modcb`, KB-rounded) | **13 KB** | **12 KB** | **-1 KB** |
| ...its settings core (`MODS_SIZE`, what a desktop gesture reads) | 2,650 | 2,639 | -11 |
| **CTRL.DRV kern_small** (unpacked; ships `CZ`) | 4,406 | **4,383** | **-23** |
| ...its claim | 5 KB | 5 KB | 0 (310 over 4 KB at base) |
| CTRL.DRV kern_emu | 12,347 | 12,114 | -233 |
| kern_big resident (`.text` 43,738, `.bss` 5,212, `.cold` 38,690, `.lowbss` 5,598, `.vgabuf` 336) | | | **0** - `kernsize --json` identical |
| kern_small resident (`.text` 31,967, `.bss` 3,063, `.cold` 22,515, `.lowbss` 2,868) | | | **0** - identical |
| kern_emu resident | | | **0** - identical |
| `.ovl` / `.boot2` / `KERN_SIZE` | | | 0 / 0 / 0 on all three |
| ROM arm (`make rom-big`) | 139 module sites in 7 modules | 139 in 7 | builds (taken at batch 1 and at the tip); 778 bytes of the window spare |

### What the concept came to, and against the brief's 30%

| | bytes out (kern_big CTRL.DRV) |
|---|---:|
| Ports view (`dde6b19`), its routines and hooks | -125 |
| Force Color (`c461c5b`): `VGC_FORCE` -11, its click's door -2 | -13 |
| Windows theme (`c4b43d0`): the toast | -10 |
| `DA` key / `HDD.DRV` autoload | 0 |
| **the concept's own code** | **-148** (14% of ~1,045) |
| SECONDARY, the rest of ctrl.inc and shutdown.inc: shared arrow and button tails, the three doors' other sites, `cp_skipset`, the shutdown exits | -85 |
| **CTRL.DRV in all** | **-233** (22% of ~1,045) |

**Under the 30% asked for, and said plainly.** The Ports view's 487 bytes
that are left are mostly calls with coordinates in them and 84 bytes of
strings and pin lists a user reads; what came out came out of its plumbing.
What the pass bought instead is the KB below.

**The claim is the number that matters.** CTRL.DRV's image had grown 58 bytes past 12 KB, so
the panel claimed 13 KB of heap on every machine while it was open. The
module's bss (`cpc_buf`, 152 bytes) has to fit the image's KB rounding
(kernel.asm's MODC_BSS assertion, MODULE-SELFCONTAIN-PLAN 3.2), so the image
had to come to 12,136 or under for the claim to fall - 210 bytes - and below
that line nothing more is bought until 11,112. It is at 12,113: **23
bytes of rounding left for the bss**, which `soak -k buildmatrix` confirms
for every knob build (none of them grows CTRL.DRV past it; ANIMOFF pads its
own image and KERN_EMU is one byte longer).

## TAKEN

### Batch 1 (`eb03515c`) - the concept, and the shapes it shared

kern_big CTRL.DRV 12,346 -> 12,124 (-222), kern_small 4,406 -> 4,389 (-17).

**The Ports view (SPEC.md 31.15), `dde6b19`'s 686.** Its routines came to
610 bytes and are 487 now (-123), with the hooks into the Drivers page net 0:

* `cp_pcap` + `cp_phex` + `cp_parrs` + `cp_parr` (181 bytes, and every
  caller called the first and the third back to back) are ONE routine,
  `cp_pval` (148): one `kentm_si` bank, one `cp_pty`, two call sites instead
  of four. The arrows' two answers are pushed as FLAGS (`cmp`/`pushf`,
  `cmp`/`cmc`/`pushf`) before the cell registers are loaded, so no register
  carries them; the down cell is drawn first because its flags are on top.
* the hex digit is `cmp al, 10 / sbb al, 69h / das` inline, twice (10 bytes
  against a 9-byte helper and two calls). Checked for 0..15 against the
  8086's DAS rules; `AAM 16` would have been shorter and is refused - a V20
  ignores AAM's base.
* `cp_ppin`: the sound row's pin is the byte UNDER the Ethernet row's
  (`CFG_EPIN = CFG_SPIN + DRV_PBLOB_SZ`), so it is `mov si, drv_cfg +
  CFG_EPIN / sub si, bx` with BH = 0 - 6 bytes for 11. driver.inc asserts
  `DRV_PBLOB_SZ = 1` beside the definition (the only place a `%if` can see
  both). Every caller holds the row in BX with BH = 0: the paint's `mov bx,
  1`, the click's `cbw / xchg bx, ax`.
* `cp_pidx` returns AH = the length from `cp_plist` and counts in AL with
  `inc ax`, no banks (24 for 36); `cp_pty` is `lea`/`or`/`add` with no
  multiply and no bank (11 for 13).
* `cp_port_paint` letters both labels with `cp_blbl`/`cp_lbl` records instead
  of staging them in the row loop, and walks the rows from the bottom (`dec
  bx / jns`). It is entered by a JUMP from `cp_drv_paint`, straight below it,
  and ends on that routine's `kentm_si` bank (96 -> 73). The pixels are the
  same: the two labels and the two rows share none.
* `cp_port_click` is entered the same way from `cp_drv_click` (no bank of its
  own), and finds the cell by `div` by the cells' 16-pixel pitch, the row's
  own shape (97 -> 84). x is pane-relative and under 4,096 on any desktop the
  drag ladder can report, so the divide cannot overflow - the y divide above
  it, which shipped, rests on the same bound.
* `cp_drv_phit` (26) is inline in `cp_drv_click`, and on the heading row a
  click OFF the button still falls through to the list: the up arrow's band
  reaches into that row (`cmp dx, CP_DBY1 + CP_DSAH / jb .up` has no top),
  so refusing there would have been a behaviour change.
* `cp_drv_ctl1` reaches the button with a tail `je cp_drv_pbtn` ahead of its
  own bank; `cp_drv_pbtn` sits above it for the short reach.

**Force Color (`c461c5b`)**: `VGC_FORCE` (driver.inc's macro, expanded once
as `cp_vgc_force` in the settings core) banks DS instead of ES and addresses
0:487h..489h with no segment prefix (-5), and the palette loop takes the 2/3
bit out of the shift's own carry (`shr ah, cl / sbb al, al / and al, 2Ah`,
CL = 3..1) where it tested two bits (-5), `inc bx` for `inc bl` (-1): 113 ->
102. The 192 DAC writes are the same values in the same order (checked
entry by entry against the old loop, and `soak -k vgacolor` reads the DAC
after a tick and asserts the EGA table exactly). The settings core shrinks
with it (2,650 -> 2,639): a desktop gesture reads 11 bytes fewer.

**Windows theme (`c4b43d0`)**: the refusal toast was the push/stage/pop that
`sc_m_say` already is for an image string (ES = CS) - 18 bytes to 8.

**`DA` key / `HDD.DRV` autoload (`edd0efb`, `8cead00`)**: nothing taken. The
`DA` hunk is one `mov byte [bx+DRVR_ANS], 1`; `8cead00` has no ctrl.inc hunk
(its +19 is driver.inc's `drv_errstr` and boot code, not this module's).

**Shared with the rest of ctrl.inc (SECONDARY, taken - nobody else edits
ctrl.inc this pass):**

* `cp_arrcell` - one arrow cell's interior, frame and triangle, falling into
  `cp_drv_tri`. Four drawers spelt the tail out: the Drivers list's arrows
  (`cp_drv_arrow1`, 83 -> 56), the item list's (`cp_larrow`, 64 -> 51, which
  also ends on `kretm_si` now instead of five pops) and the Ports view's two.
  `cp_drv_arrow1`'s held test answers as a byte (`sub al, [cp_down] / cmp
  al, 1 / sbb al, al`) instead of a call and a branch.
* `cp_btfill` / `cp_btdn` - the button painters' common tail (`mov di,
  OS88UI_FILL / call cp_dnbtn / call cp_bemit / jmp kretm_bp`), jumped to by
  Screen Saver, Set Primary, the clock's + and -, Test and Ports / Back
  (-33 kern_big, -9 kern_small). Set Primary stages before `cp_dnbtn` now;
  `cp_stage` keeps AX and DI and `cp_dnbtn` keeps SI, so the order is free.
* near doors for the image's three commonest five-byte stores: `cp_black`
  (ten sites, plus `cp_blbl` and `cp_brun` themselves), `cp_brun` (black then `cp_run`, 5 sites) and `cp_wdset`
  (`[cp_wdirty] = 1`, 8 sites). A door writes no flag, which the Force Color
  click relies on (its `jz` is the `xor`'s).

### Batch 2 (`c870f1f3`, and `a5210db3` for its shutdown half)

kern_big 12,124 -> 12,113 (-11), kern_small 4,389 -> 4,383 (-6).

* `cp_drv_click`'s button x test is one unsigned compare of `x - CPD_PBX1`
  against the width (-2; brute-forced equal to the signed pair over all
  65,536 values of CX).
* `cp_skipset` stores 0 once and adds the 1 for a skip, carrying ZF past the
  store (-2).
* `shutdown.inc` (CTRL.DRV's, included from ctrl.inc): the shutdown entry's
  two refusals jumped NEAR to a `retf` 160 bytes on. The first refusal is a
  `jnz` over a `retf` now, and the second reaches that `retf` short (-6,
  both kernels). The first spelling put the `retf` ABOVE the entry instead
  and failed a row - see DEFECTS FOUND.

## REFUSED

* **`[cp_dview]` folded into bit 7 of `[cp_dtop]`** - the Ports view's one
  RESIDENT byte (`.text`, kern_big). Every reader of `[cp_dtop]` runs only
  while the list is shown, so it is sound, and it is -1 resident. Refused:
  one byte, `tests/netcoexist.py` reads `cp_dview` by name and would have to
  learn the bit, and it shifts every `.text` byte after ctrl.inc's data by
  one - an odd move under hot code that PERFORMANCE.md Part 7 asks to be
  priced before it is taken. For the next pass if it is going through
  `.text` anyway.
* **A shared button RECORD** (`call cp_btnr / dw x1,y1,x2,y2,str / db id`):
  ~50 bytes of reader against ~23 a site at three fixed-position sites -
  under the tail sharing that was taken.
* **`cp_white`, a door for `[gfx_color] = CWHITE`**: three sites plus
  `cp_wfill`, -2 net. Not worth a routine.
* **The caption built by a 3-digit `rol` loop**: 40 bytes against 37.
* **A drop-down for the pins** - SPEC.md 31.15 refuses it already (28 bytes
  of `cp_sbuf` do not hold twelve items).
* **The box frame drawn by `cp_pval`** (it already holds the row's y1/y2):
  the click path would then redraw an unchanged frame on every step - the
  same pixels, but a primitive (~3.4 ms on the 5150) per click for 6 bytes.

## WANTS AN ABI BREAK

Nothing in this concept is worth one by itself. Recorded so nobody
re-derives it:

* `SP` and `EP` are two SYSTEM.CFG keys of one byte each (4-byte record
  header apiece, and a `drv_blob_map` row each) because a blob belongs to a
  ROW (SPEC.md 51.9.1). That is the right shape; merging them would make a
  driver read another driver's blob.
* `VC` is a whole key for one bit (SPEC.md 39.28 explains why it is not a bit
  of `VM`: a ver bump resets everybody's display arrangement). A flag day
  that bumps `VM` anyway could take it as a bit and save the key entry and
  its 4-byte record.

## CROSS-FILE

All CTRL.DRV bytes (the settings core), none resident, none mine:

* `desksc.inc` (`sc_m_add`): `jc .noroom` assembles RELAXED (5 bytes) -
  up to -3 if the target can be brought within reach or a nearer `stc`/`ret`
  shared.
* `desksc.inc`: `jc .bad` twice (after `fcpf_fcp_goto` and `dsk_path_x`),
  both relaxed - up to -6 together.
* `driver.inc` (`cpc_fdtest`): `jc .out` after `mmf_mem_claim`, relaxed -
  up to -3.
* `tests/shutdown.py`: the write-protect leg's 5-byte patch over the 3-byte
  `cp_cfg_save` stub (DEFECTS FOUND) - a test fix, 0 bytes.
* SPEC.md 2.8.7's table still says `MODS_SIZE` = 2,447 bytes, 5 sectors; it
  is 2,639 at the tip (2,650 at the base), 6 sectors.

## ROWS RUN

Every batch: `make -j2`, `make -j2 small`, the fast tier (43/43 each time),
`tools/stkbalance.py` over ctrl.inc, driver.inc and shutdown.inc base
against tip (the same 3 pre-existing driver.inc paths, nothing new),
`checkdocs`. At the tip: `make emu`, `make rom-big`, kernsize `--json` on
all three kernels identical to the base.

**Equivalence by pixels** (scratch harnesses, not committed: a walk of every
Control Panel page that holds and releases every button and arrow the pass
touched, steps both Ports lists to both ends and back, ticks Force Color
twice, and loads the RAM disk so the item list scrolls; the panel's rect
captured off MartyPC's rendered frame after each of 41 steps, base build
against tip build):

| machine | kernel | steps | result |
|---|---|---:|---|
| `os8088_xt_vga` | kern_big | 41 | identical, but for Date/Time's live seconds |
| `os8088_5150_both_gla` (CGA + Hercules, the 1bpp greying) | kern_big | 41 | identical, but the seconds |
| `os8088_xt_vga_hdd` (ten list rows: `cp_larrow` held and stepped) | kern_big | 9 | identical |
| `os8088_5150_both_gla` | kern_small | 8 | identical, but the seconds |

**Rows** (`os88test.py soak --marty-jobs 1 -k ...`):

* after batch 1: `vgacolor` (reads the DAC after a Force Color tick and
  asserts the EGA table exactly - the `VGC_FORCE` rewrite's proof), `thm95`,
  `win1bpp`, `dispthm`, `drvscroll`, `drvup`, `cplistrow`, `cpnames`,
  `cpnameshdd`, `fddpage`, `cpup`, `cppromise`, `covoxcp`, `hdauto`,
  `fsclass`, `hddcp`: **16/16 ok**.
* after batch 2: `cpup`, `drvscroll`, `drvup`, `netcoexist` (QEMU: its
  `pinned` arm drives the Ports view by the mouse, steps the card to 2A0h,
  reads `EP` back off the written disk and gets DHCP), `mpupin`: ok;
  **`shutdown` FAILED** - the test defect below. After `a5210db3`:
  `shutdown` ok (VGA, CGA, Hercules) and `tests/shutdown.py --small` ok.
* `buildmatrix` at the tip: **ok** - every knob kernel and knob module
  assembles, so none grows CTRL.DRV past the 23 bytes its bss has left.

## DEFECTS FOUND

**`tests/shutdown.py`'s write-protect injection overwrites two bytes it
does not own (a TEST defect, not fixed - it is not this pass's file).** Batch
2's first
spelling of the shutdown exits put the shared `retf` (`sd_out`) ABOVE
`modc_e_shutdown`, moving the entry from the first byte of `.modu` to the
second - nothing else in the image changed. `soak -k shutdown` then failed on
VGA at its write-protected-save leg (an injected `cp_cfg_save` that answers
`FERR_WPROT`, then Enter): the prompt closed and `[cp_dsave]` read 2, but
`[mod_tab]` still named CTRL.DRV 90 guest seconds later, where `cpf_shutdown`
drops it. Three runs, three failures; base passes; the SAME tree with only
shutdown.inc reverted passes; the shipped spelling (the `retf` inside the
entry, the entry back at byte 0 of `.modu`) passes on VGA, CGA, Hercules and
kern_small. The Escape and Cancel legs passed either way, so it is the
flush-then-refuse path. **The cause is the test's injection**: it writes
FIVE bytes (`mov ax, FERR_WPROT / stc / ret`) at `cp_cfg_save`, which in
CTRL.DRV is a THREE-byte `jmp cpc_save` (driver.inc) and the LAST thing in
`.modc` - offset 2,636 of a 2,639-byte settings core. The other two bytes land
on `.modu`'s first two. At the base those are `modc_e_shutdown`'s own `call
sd_confirm`, already executed when the patch is made, so the clobber is
silent; with `sd_out` there they were the `retf` the refusal then jumped to.
The kernel is fine either way; the shipped spelling keeps the entry at
byte 0, so the row passes as it stands. The commit message of `a5210db3`
says "not root-caused" - this paragraph is the root cause, found after it.
**Owed to the test**, by whoever owns it: patch `cpc_save` (the body), or
write a 3-byte `stc / ret` plus the code in AX some other way - and a row
that patches a module ought to assert the patch fits what it replaces.

One near miss, recorded because it is the kind a size pass makes: the
first spelling of the inline button hit test refused every heading-row click
off the button, which would have silently stopped the Drivers list's up
arrow answering in the heading row (its band has no top). Caught reading
`cp_drv_click` before it was built; the shipped test falls through.
