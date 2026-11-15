# Driver size pass 1: `kmod`, the kernel's on-demand modules

Branch `dsp1-kmod`, cut from `072679b1`. Agent `kmod` (BRIEF.md 2, 2.1) owns
the code that assembles INTO a module image (`.modc`/`.modu`/`.modf`/`.modl`/
`.modh`/`.modk`/`.modx`/`.modw`/`.modj`/`.modn`/`.mods`/`.modfs`/`.modp`/
`.modd` and their nobits tails), cut out of each kernel by `tools/os88mod.py`
(SPEC.md 2.8).

**This pass took nothing.** The coordinator's wrap-up arrived after the base
builds (all four kernels) finished and while the first candidates were being
priced, before any item had been started, so under its instruction this note
records the base, the module-to-source map, one rule-shaping fact about the
resident loader, and the candidates found, for the next pass. No code
changed; the only files changed are this note and `docs/INDEX.md`.

## 1. BASE AND TIP

Measured from this worktree's own `make -j2`, `make -j2 emu`, `make -j2 mega`
(kern_small comes out of the full `make`) at `072679b1`, and
`tools/os88diskuse.py --save`. Tip = base on every row: no module byte moved.
"image" is image + bss tail (MODB_TAIL: a module's bss is zeros in its own
file, SPEC.md 2.8.1), which is what the claim holds; "file" is what the disk
carries (`'CZ'`-packed, except CTRL.DRV on kern_big/emu/mega, plain by design,
SPEC.md 2.8.7).

| module | kernel | image | file | 360KB clusters | delta |
|---|---|---:|---:|---|---:|
| CTRL.DRV | big | 15,459 | 15,459 | os8088-360: 31 | 0 |
| | small | 5,671 | 5,344 | small360: 6 | 0 |
| | emu | 15,477 | 15,477 | | 0 |
| | mega | 15,649 | 15,649 | | 0 |
| DISKOPS.DRV | big | 10,216 | 8,984 | os8088-360: 18 | 0 |
| | small | 7,195 | 6,458 | small360: 7 | 0 |
| | emu | 10,216 | 8,984 | | 0 |
| | mega | 11,395 | 9,409 | | 0 |
| STIKMENU.DRV | big | 7,926 | 3,579 | os8088-360: 7 | 0 |
| | emu | 7,926 | 3,579 | | 0 |
| | mega | 8,395 | 3,736 | | 0 |
| HIBER.DRV | big | 7,715 | 6,168 | os8088-360: 13 | 0 |
| | emu | 7,715 | 6,168 | | 0 |
| | mega | 11,581 | 9,299 | | 0 |
| FSCHECK.DRV | big | 6,298 | 5,280 | util360: 6 (not on os8088-360) | 0 |
| | emu | 6,287 | 5,270 | | 0 |
| | mega | 6,283 | 5,266 | | 0 |
| DESK.DRV | big | 4,432 | 4,141 | os8088-360: 9 | 0 |
| | emu | 4,432 | 4,141 | | 0 |
| | mega | 4,688 | 4,164 | | 0 |
| FORMAT.DRV | big | 3,179 | 2,945 | os8088-360: 6 | 0 |
| | small | 1,280 | 1,226 | small360: 2 | 0 |
| | emu | 3,179 | 2,944 | | 0 |
| | mega | 3,432 | 3,014 | | 0 |
| DOCK.DRV | big | 2,232 | 2,030 | os8088-360: 4 | 0 |
| | emu | 2,232 | 2,029 | | 0 |
| | mega | 2,303 | 2,064 | | 0 |
| EXTD.DRV | big | 1,927 | 1,830 | os8088-360: 4 | 0 |
| | emu | 1,927 | 1,830 | | 0 |
| | mega | 1,862 | 1,779 | | 0 |
| WIN95.DRV | big | 1,570 | 1,316 | os8088-360: 3 | 0 |
| | emu | 1,570 | 1,316 | | 0 |
| | mega | 1,611 | 1,333 | | 0 |
| SERIAL.DRV | big | 919 | 848 | os8088-360: 2 | 0 |
| | small | 919 | 848 | small360: 1 | 0 |
| | emu | 919 | 848 | | 0 |
| | mega | 1,010 | 877 | | 0 |
| FILECP.DRV | small | 1,876 | 1,710 | small360: 2 | 0 |
| FDLG.DRV | small | 1,590 | 1,529 | small360: 2 | 0 |

The 360KB system disks at base (and tip): `os8088-360.img` 37 of 706
clusters free (512-byte clusters; the kern_big modules above are 97 of the
used ones), `small360.img` 17 of 354 free (1KB clusters; its six modules are
20). Cluster delta: **0 / 0**.

**Resident proof.** No code changed, so the four `kernel.bin`s are the base's
(kern_big `KERNEL.SYS` 105,776, kern_small 63,328 md5 `48967980...`, kern_emu
106,144 md5 `6d9942da...`, kern_mega 146,497 md5 `a81d82c3...`, at
`072679b1`; kern_big `kernsize --json`: text 39,199, bss 4,878, cold 37,677,
ksize 95,232 against budget 100,864).

### 1.1 Which source assembles into which module

Found by the `section .modX` switches (kernel.asm 2371-2444 declares them):

| section | module | source files with code in it |
|---|---|---|
| `.modc` | CTRL.DRV settings core | ctrl.inc, desksc.inc, driver.inc |
| `.modu` | CTRL.DRV panel | ctrl.inc, desk.inc, dock.inc, driver.inc, fdlg.inc, sched.inc, shutdown.inc, vidsel.inc, wm.inc |
| `.modf` | FORMAT.DRV | ascw.inc, diskw.inc |
| `.modl` | DISKOPS.DRV | diskops.inc, compress.inc |
| `.modh` | HIBER.DRV | hiber.inc, driver.inc |
| `.modk` | DOCK.DRV | dockmod.inc |
| `.modx` | EXTD.DRV | extmod.inc |
| `.modw` | WIN95.DRV | winmod.inc |
| `.modj` | DESK.DRV | deskmod.inc |
| `.modn` | STIKMENU.DRV | stikmenu.inc |
| `.mods` | SERIAL.DRV | serial.inc |
| `.modfs` | FSCHECK.DRV | fsck.inc |
| `.modp` / `.modd` | FILECP.DRV / FDLG.DRV (kern_small) | filecp.inc / fdlg.inc |

Most of these files are mostly RESIDENT code with a module section inside
them; only the lines under a `section .mod*` switch are this agent's.

## 2. TAKEN

None (see the preamble).

## 3. REFUSED

None reached a decision.

## 4. WANTS THE OWNER

None.

## 5. WANTS AN ABI BREAK

None.

## 6. CROSS-FILE

- **A `.modc` byte is a resident byte, though not a resident SIZE.**
  `kernel/mod.inc:609` (`mod_need`, `.cold`, resident) loads
  `mov bp, MODS_SIZE`: the settings core's length is an immediate in the
  resident loader, because MOD_SETS reads only CTRL.DRV's first `MODS_SIZE`
  bytes (SPEC.md 2.8.7). Any saving in the settings core (`.modc`: ctrl.inc,
  desksc.inc, driver.inc's `.modc` parts) changes those two bytes of
  kern_big's (and emu's and mega's) `.cold`, so `cmp` of the resident
  sections fails although `kernsize` reports no size moved. Under BRIEF 2.1
  ("may not move by a byte") the next pass should either confine itself to
  `.modu` and the other images, or get the coordinator's ruling that an
  immediate's value is not a moved byte. `.modu` savings, and every other
  module, leave the resident image byte-identical (the map that names the
  module offsets, `.modmap`, is cut off with the modules).
- **The module directory hint.** `mod_need` sizes the claim from the file's
  directory entry, so a module that shrinks across a 512-byte granule shrinks
  its claim with no resident change (SPEC.md 2.8.1).
- **Attach-only code**: modules have no attach; each is read when its feature
  is asked for and dropped at the feature's end (SPEC.md 2.8.3), so the
  section 0.2 price is **0** for all of them.
- **FSCHECK.DRV** is `kernel/fsck.inc` (`.modfs`); the hdd agent's note
  (docs/plans/dsp1/hdd.md 6) left it to this agent. Its four call/ret tails
  are in 9 below.

## 7. ROWS TO RUN

None: nothing changed. For the next pass, the rows that cover the modules
named in 9 are, per `tests/suite.py`: `modcache`, `moddrop`, `dockmodule`,
`hibernate`, `stikmenu`, `desksc`, and FSCHECK's `fsckdisk`, `fsckhd`,
`fsckmega`.

## 8. LOOKED

No boot: nothing changed.

## 9. NOT REACHED

Found or queued, priced, not taken. Every one is a module-only change; the
resident claim in 6's first bullet applies to the `.modc` ones (none below is
`.modc`).

**`call X / ret` tails to NEAR callees in module sections** (found by a
section-aware scan of `kernel/*.inc`: a `call` line followed by `ret` while
the current section is `.mod*`). `jmp X` saves 1 byte (near, 3+1 -> 3) or 2
(short, -> 2) per site, image and roughly the same in the file. Each needs a
read that the callee does not look at its own return address or stack depth
and that no label falls into the `ret` from elsewhere:

| site | module | form | est. image |
|---|---|---|---:|
| `ctrl.inc:3932` `cp_vid_rowok` | CTRL (`.modu`) | `call vid_avail_test / ret`; the whole body, so its callers could call `vid_avail_test` directly | -1 to -4 |
| `extmod.inc:1226-1236` `ext_kapply_eq` and two siblings | EXTD | three `call ext_equip`/`ext_apply`/`ext_desk_union` / `ret`; the first is followed directly by `ext_kapply:`, so it may collapse to a fall-through | -3 to -6 |
| `fsck.inc:2248, 2356, 2379, 2421` | FSCHECK | `call fsk_save`/`fsk_q0ds`/`fsk_flush`/`fsk_wr` / `ret` (ksp20 disk.md listed these as "module bytes, left for a module pass") | -4 to -8 |
| `hiber.inc:1574` `hbm_findimg` | HIBER | `call hbm_cb / ret`, comment says CF, BX, DX:CX are the answer | -1 to -2 |
| `shutdown.inc:237` | CTRL (`.modu`) | `call sd_disp / ret` | -1 to -2 |

Not candidates, and why: `dockmod.inc:1591` is `push cs / call` (a far call
into a `retf` routine, so it cannot be a tail jump); the twelve `call
KERNEL_SEG:`/`COLD_SEG:` tails (desksc, diskops, diskw, shutdown, stikmenu,
winmod) are far calls followed by a NEAR `ret` and cannot become `jmp far`
either.

**Total priced: about 10 to 22 image bytes across CTRL, EXTD, FSCHECK and
HIBER**, none crossing a granule; all cold (panel, placement setup, the
checker's repair, hibernate's image lookup). The larger shapes (BRIEF 0.2's
callee-saves, shared tails, dead reloads; DISKOPS's and STIKMENU's poorly
packing images - STIKMENU 7,926 image against 3,579 file is mostly its bss
tail and packs well, DISKOPS 10,216 against 8,984 is code) were not reached.

## 10. DEFECTS FOUND

None.
