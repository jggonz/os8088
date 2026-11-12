# Kernel size pass 20: the record, and what is left for a twenty-first

**PASS 20 HAS LANDED** on `kernel-size-20`, cut from `elendilon` at
`8ef75fa1` on 2026-11-12. Named `-P21` for the pass it hands to, as every
record before it. The brief is `docs/plans/completed/ksp20/BRIEF.md`; each
agent's note is beside it and is the authority on its rows.

## 0. The brief and the outcome

The owner's words: *"This kernel size pass will go over the recently
added/enabled speed, big-pass etc work. It may also include side forays to
find a few more bytes. The goal is to save ~300b, and to get kern small down
one more rung (it should be within 200b of that; normally we do not aim for
rungs, but kern small is approaching its final form and size, leading to
there being value for a rung there)."* ABI breaks were allowed without a
version bump; **none was needed** - no agent moved `apps/os88api.inc`,
`drivers/os88drv.inc` or the C SDK, and `tools/abi.lock.json` is untouched.

**Both goals met, and no kernel grew.**

### Kernel resident, assembled (`tools/kernsize.py --json`)

Resident = `.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf` + `.vga`
(+ `.mega`).

| kernel | base `8ef75fa1` | tip | delta | `KERN_SIZE` |
|---|---:|---:|---:|---|
| **kern_big** | 90,086 | **89,672** | **-414** | 95,232 (unchanged; accrued 486 -> 72 of 512) |
| **kern_small** | 55,976 | **55,465** | **-511** | **57,856 -> 57,344 - one rung down** (accrued 168 -> 169 of 512 at the new rung) |
| kern_emu | 90,332 | **89,918** | -414 | **95,744 -> 95,232** - a rung down too |
| kern_mega | 128,895 | **128,507** | -388 | conventional 61,440 of 68,096 (unchanged); conventional sections 55,795 -> 55,642 (-153) |

`make budget-check`: kern_big 95,232 of 100,864 (5,632 spare), kern_small
57,344 of 64,000 (6,656), kern_emu 95,232 of 100,864, kern_mega 61,440 of
68,096. Module images: CTRL.DRV -2, EXTD.DRV -2, FILECP.DRV -1; nothing grew.

## 1. Who took what

| agent | kern_big | kern_small | the largest items | note |
|---|---:|---:|---|---|
| **draw** | -57 | -74 | `gfx_sub_arm` on `kent_bp` (-17), `ico_core`'s dead reloads (-13), `gfx_ls_box` (-12 small), the planar cursor walks keep ES (-4 `.vga`), `sw_patcol`'s one-store form | draw.md |
| **wmui** | -63 | -64 | `wm_paint_all` jumps into `wm_paint_dmg` past its save (-5, faster), `menu_relayout` by `stosw` (-19), the bar cell through BP (-15/-12), `ui_dispatch`'s command base computed (-9), EXTD's slot 10 reused | wmui.md |
| **disk** | -93 | -109 | the read-ahead's pick/scan/prefetch (byte multiplies and divide, no empty-slot tests, saves only what callers read), `dskw_mkclus` in one loop (-9), `dsk_find_x`'s token by pop/push (-6) and DSK_DSCAN tail (-6) | disk.md |
| **files** | -130 | -183 | kern_small's two header buttons as one helper (-48 small), the paste union through wm.inc's `wm_rect_empty`/`wm_runion` (two private `rd_tab` rows), relaxed jumps written `short`, `fm_scrollpaint`'s band in registers (-6 `.bss`, faster), kern_small's zone mask as one word | files.md |
| **sys** | -67 | -74 | the door's out-of-range refusal through the existing refusal entry (-7), `task_spawn` on the KENT ladder (-14), class 0's stack-class row gone, `inst_task_die` falls from `inst_pkg_alive` (-8), the module cache's held row, the overflow panel's words into `api_name` | sys.md |
| coordinator | -4 | -7 | the agents' CROSS-FILE items: `sched_mode_get` deleted, its two CTRL.DRV callers read `[sch_coop]` (SPEC.md 8.2's table moved with it); kern_small's `fm_brun` makes its own far call and `ct_cw_font_run` goes there | f7ad1e9e |

## 2. Speed and the disk

* Every H1/H2 change is cycle-neutral or faster with its count in the note;
  `sch_isr`, `sch_switch` and the door's dispatch are byte-identical. Two
  H3 items cost a few clocks a primitive (draw: `cur_lazyck`'s `kret_si`,
  `icon_draw_x`'s shift), each under 1% of its operation.
* **No `int 13h` call added or removed** - the read-ahead's picks, fill and
  prefetch lengths, cache width and write order are unchanged (disk.md,
  checked by reading; one look-boot of kern_small listing APPS and launching
  Calculator).
* `hotpar` green on both kernels at every merge. sys removed four existing
  parity pads, each paired with a real 1-byte saving: the pinned routines
  keep their parity, the unpinned code between each pad and its partner now
  sits at an odd shift (a cost on an 8086/V20/286 bus, none on the 5150's
  8088).

## 3. Behaviour

Byte-identical everywhere, with one recorded difference: **the paste union
is now signed**, as wm.inc's damage rects are (files.md item 6). The rect is
identical on the glass; for a window dragged partly off the left edge the
new union reaches columns the old unsigned one missed, so that repaint is
the same or more correct. And kern_dos (not one of the four kernels; its
`.bss` is not zeroed at load) may spare a never-filled read-ahead slot in a
different order on its very first pick - the cache's contents are correct
either way (disk.md, DEFECTS).

## 4. Wants the owner (still open from pass 19)

1. **`OSAPI_BATCH_END` withdrawn** (-18 resident big/small/emu).
2. **`W_DISP`'s offset dropped** (about -20 resident, about +47 clocks a
   callback).

Neither was taken.

## 5. What is left

* **`[menu_ent]`** (-6): the kernel no longer reads it, but `uilat`,
  `weavelat` and `a2uilat` break on it; move their stop to `menu_cnt` and it
  goes (wmui.md REFUSED 1).
* **`wm_band`'s `cbw`** (-1), held for parity: pair it with any odd
  kern_big-only byte in wm.inc/menu.inc/fprog.inc. **`ui_task`'s `jmp
  .yield`** fits `short` (-1, kern_big), a `hotpar` routine: the natural
  partner.
* **`drv_notice`'s far frame for `cp_open_x`** would let kern_big's
  `cpf_cp_open` thunk go as kern_small's did (-3 `.cold`).
* **`desk_dmg_zones_x` on kern_small** banks SI round `cw_wm_zone_r`; if SI
  is dead after both `wm_paint_dmg` sites, -2.
* `disk.inc`'s stale comment naming `inst_icon_ptr` as a `dsk_ico` user.
* **The largest refused item**: `inst_icobuf` as `dsk_ico` (-64 every
  kernel) - unsafe while a worker's dock repaint can meet a mount's staging
  window (sys.md REFUSED 1).
* NASM never shrinks a jump it relaxed: after every merge, re-scan for a
  relaxed `jcc`/`jmp` now in short reach - each is a free 1 or 3 bytes, and
  only `short` in the source collects it (files.md).
* An odd byte between `mou_p2_off` and `dsk_img16` moves the hot mouse ISRs
  on kern_big through `disk.inc`'s self-adjusting pad (draw.md).

## 6. What was run (the final pass, once)

- **The deduplicated union of every agent's named rows**, once
  (`tools/os88soak.py`, run `20261112-080400`, 175 rows, with Bochs, the C
  toolchain, the automation-enabled 86Box and QEMU 9 installed for it):
  **171 ok, 3 FAIL, 1 SKIP** (`win16file`: the Win16 fixtures are not in this
  container).
  * `dirwsize`: a SOURCE check whose pattern wanted `div cx`/`mul cx`; the
    disk agent's byte-wide solve (`div cl`/`mul cl`, AX <= half the arena in
    KB, so the quotient fits AL) is correct. The row now accepts either width
    and refuses a mismatched pair; broken on purpose both ways, red each time
    (`9044a172`).
  * `win1bpp`: a TEST defect. Its SHAPE phase settled the SCREEN after
    opening Missile Command, which plays in its window, so it passed only
    when the game ended inside settle's 360 guest seconds - which the build
    number decides: `f7ad1e9e` passed 3/3, `9044a172` (a test file and the
    build number apart) failed 3/3, the base passed 3/3; a picture at the
    failure shows the game in play. It now quiesces on the window records it
    reads (`9f715431`): green in 15.7s.
  * `fatwpin`: **fails at the base too** (`os88bisect sample --at 8ef75fa1`:
    BAD 1/1) - its `FATWNONE=1` knob kernel never reaches a desktop. Not this
    pass's; open for whoever owns the FAT window.
- **`test-full`, once: 65 ok, 1 FAIL** - `api-abi-all`, stale `build/emuk`
  and `build/megak` (a break-on-purpose check had touched `kernel/disk.inc`'s
  mtime after they were built); rebuilt, green.
- `make`, `make small`, `make emu`, `make mega` and `make budget-check` at the
  tip; the fast tier 44/44 with `hotpar` at every merge.
- Not run: a MartyPC before/after timing pass (`os88hot ab`). No agent
  touched a path whose speed it could not count, and every hot-path change
  carries its count in its note; the A/B is the owner's call.

## 7. Method lessons

* **Five agents by FILE, none on a concept, worked**: no merge conflicted
  beyond `docs/INDEX.md` (regenerate after every merge - `checkdocs` fails
  `make` on a stale one) and two `rd_tab` lines.
* **A source-pattern row is a second reader of the code; a size pass that
  rewrites the arithmetic must teach the row the new spelling AND re-break
  it** - `dirwsize` had to learn that the multiply must be as wide as the
  divide, or `div cl` / `mul cx` (a CH nobody set) would pass.
* **One deterministic emulator run per build is one sample of the timing,
  not three**: `os88bisect sample -n 3` at one commit repeats the same run.
  A row that flips between adjacent builds with no kernel difference is a
  timing-sensitive row - look at the picture before bisecting further.
* **Agents ran no rows** (CLAUDE.md's batch rule): the whole pass's emulator
  time was one 26-minute union, `test-full` and the follow-ups above.
