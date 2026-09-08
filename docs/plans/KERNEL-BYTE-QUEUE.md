# What the byte pass left — the queue, costed

The byte pass over the merge window (four commits, `-539` sections and a
512-byte rung on both kernels) took the findings that were measured and
verified. This is what it did **not** take, with the arithmetic attached and
the blockers named, so the next round is work rather than a prompt.

Numbers are against the tree at the end of that pass:

    kern_big    text 50,636   bss 5,959   cold 37,998   lowbss 9,182
                KERN_SIZE 110,592   .text+.bss 8,941 left of KERN_CODE_MAX
                accrued: image 275/512   cold 110/512   low 478/512
    kern_small  text 39,144   bss 4,823   cold 26,789   KERN_SIZE 78,848

---

## 1. The `cw_` shim sweep, second cut — the largest item

`kernel/kernel.asm` carries **90 `cw_` shims** after the first cut, each
`call <body>` + `retf`, four bytes. `cw_mem_disp` is `call bp / retf` — a
GENERIC trampoline that happens to be named for its first customer — so any
shim with ONE call site can be deleted for **-4 `.text`** at a cost of **+3**
at the site (`mov bp, <body>`, then the same 5-byte far call).

`.text` is what binds. A site in `.cold` or in a module image is therefore a
straight win; a site in `.text` nets -1.

### The census, and how to retake it

Three passes over the tree, all mechanical:

1. every `^cw_[a-z0-9_]+:\s*call\s+(\S+)` in `kernel.asm` — the shim and its body;
2. every reference to each name across `kernel/ drivers/ apps/ tests/ boot/`,
   ignoring the definition line itself;
3. for each single-reference site, the enclosing top-level label, that
   routine's documented `clobbers:` line, and whether it `push bp` before the
   call.

At the end of the pass that census read: **1 shim with no reference anywhere,
~50 with exactly one, 15 with two.**

### THE GATE IS THE ROUTINE, NOT THE CALL

`cw_mem_disp` needs the body's offset in BP, so the caller's BP dies at the
site. Asking "does this routine read BP after the call" is necessary and
**not sufficient** — the routine may owe BP to *its* caller. The first cut
took only sites that clear one of two bars:

- the routine already banks BP across the call and never reads it after
  (`assoc_run_x`, `cp_tick_x`, `dsk_xfer`, `ld_run_body_x` ×4); or
- the routine's contract clobbers BP outright (`assoc_handover` says
  "clobbers: EVERYTHING"; `fm_scrollpaint` names BP in its list).

### What is left, and what each one needs

**~26 sites whose enclosing routine never mentions BP at all.** They preserve
BP by accident today, so the question is whether any CALLER relies on it.
That is one read per caller, and the caller counts are small:

| routine | file | callers | note |
|---|---|---:|---|
| `menu_init`, `cp_snd_click`, `drv_boot_x`, `vid_ctx_init` | menu/ctrl/driver/vidsel | 0 found | reached by `OVLGATE1`, a page table or module dispatch — find the real entry before trusting this |
| `fm_kinit_x`, `cp_promise`, `desk_dmg_zones_x`, `fmv_repaint_all`, `fm_dgdrop`, `fm_choose`, `menu_kbnav` | files/ctrl/desk/menu | 1 each | the cheapest reads in the list |
| `fm_more_mark`, `desk_draw_zone`, `fdkf_dx`, `desk_zones_paint_x`, `ld_unreserve` | files/desk/fdlg/loader | 2 each | |
| `fm_dgxor`, `desk_zone_redraw` | files/desk | 4 each | |
| `fdlg_close` | fdlg | 5 | three shims at once if it clears |

**Refused, with the reason, so they are not re-proposed:**

- `fdlg_commit` → `wm_pkgcall`: **the body TAKES BP** (the completion proc).
  The call site's own comment says so.
- `clk_fld_str` ×4, `cp_vid_rowok`, `fm_rclick_x`, `ss_set_x` ×2,
  `ss_stop_x`, `app_state_of`, `drv_task_x`, `fdlg_hidx`: the contract
  promises to preserve BP ("clobbers: nothing else", "clobbers: AX, flags",
  …). Their shims stay.
- `gfx_blit1_x` ×5: refused twice over — BP is live across all five, and it
  is a graphics primitive, where this project does not trade cycles for
  bytes.
- `ctrl.inc`'s `.modc` sites look free (the +3 lands in `CTRL.DRV`, which
  `os88mod.py` cuts out of `kernel.bin`) and are **not**: `ctrl.inc` keeps BP
  live as the page parameter through the driver Control Panel ABI
  (`mov bp, [di+DSV_CPPAINT]`, `mov bp, [di+DSV_CPCLICK]`, and ~40 reads of
  the form `mov dx, bp`). Each one needs its dispatcher read, not the file.

### The alternative nobody has priced

A trampoline taking the body address **inline after the call** —
`call far cw_call_imm` followed by `dw body` — is 7 bytes at the site against
8, and clobbers **no register at all**, which deletes the whole liveness
question and unlocks every remaining site. It costs one trampoline (~20
bytes, reading its own return address through the caller's segment) and is
slower per call. Nothing in the tree does this today. If the queue above
stalls on liveness reads, this is the thing to cost properly.

---

## 2. `hb_probe_x` → the boot overlay — ~60 `.cold`

`hb_probe_x` (`kernel/hiber.inc`) is ~60 of hibernate's `.cold` bytes and is
called **exactly once**, from `kmain`, *between two calls that are already in
the boot overlay*:

    kernel/kernel.asm   OVLGATE1 drv_boot_x        ; ...SYSTEM.CFG's drivers
                        call COLD_SEG:hb_probe_x   ; ...a hibernation to resume?
                        ; --- the overlay is dead from here ---
                        OVLGATE1 xm_boot_x

The overlay is demonstrably alive at that instruction. Moving it out of
`.cold` relieves the footprint and costs nothing in the segment either way.

**The destination is `.ovlw`, not `.ovl`**: `.ovl` has ~55 bytes of blob
headroom (`OVL_AT` 2624 + `OVL_SIZE` 1417 against `BOOT2_PAD`, guarded in
`kernel.asm`), so ~60 bytes does not fit. `.ovlw`'s own bound is
`docs/plans/KERN-SMALL-CUT-PLAN.md` §7's mount buffers.

**The hazard to disprove is self-overwrite.** `hb_probe_x` opens
`HIBERNAT.PTR`, i.e. it does a FAT read, and `.ovlw` is loaded onto the FAT
window. The evidence it is survivable is already in the tree — `drv_boot_x`
reads `SYSTEM.CFG` from inside the overlay, and `kernel.asm` records which
buffer is the problem and why that call sits where it does. Settle that
before moving anything.

A separate refusal already stands and is correct: `hb_probe_x` may **not**
move into `HIBER.DRV`, because every installed machine would then read a
multi-KB module image before the first paint —
`docs/plans/completed/BOOT-PERF-PLAN.md` deleted exactly that class of probe.

---

## 3. `HB_NENT` 7 → 6 — and the blocker that makes it two modules, not one

`mod_fp` is `MOD_MAX * MODFP_STRIDE` where `MODFP_STRIDE = MOD_NENT * 4`.
With `MOD_NENT` 7 that is **112 bytes of `.bss` on kern_big** (`MOD_MAX` 4)
and **140 on kern_small** (`MOD_MAX` 5). At 6 it is 96 and 120 — **-16 and
-20 in the guard that cannot be raised.**

Hibernate is the module that pins it: `HB_NENT equ 7`, where `CP_NENT` is 6,
`FM_NENT` 4, `FCP_NENT` 3, `CLO_NENT` 2. `mod.inc`'s own comment says
`MOD_NENT` came down "to what the modules use" once already, and stopped one
notch short because of hibernate.

**`MOD_NENT` CANNOT BE PER-BUILD.** `tools/os88mod.py` reads it with
`re.search(r"^MOD_NENT\s+equ\s+(\d+)")` and cannot evaluate an `%ifdef`
(`docs/plans/completed/KERN-SMALL-MODULE-SPLIT.md` found this the hard way).
So the constant is one literal on both kernels, and lowering it needs
**`FD_NENT` down to 6 as well** — `FDLG.DRV` publishes 7 on kern_small. Two
modules to restructure, not one.

Two more things move with it: `mod_fpr`'s multiply chain is hand-written for
×7 (`x8 - x1`) and would become ×6 (`x8 - x2`, no worse), and the `%error` in
`mod.inc` that guards `MOD_NENT != 7` is retired by the same change — it
exists precisely to stop this being done carelessly.

The candidate seventh entry, unpriced: fold `HBE_ONUP` into `HBE_ONCLICK`
behind a selector byte. Nothing in the tree has applied technique 4 to a
module entry table before.

---

## 4. Where the rungs stand

`.cold` accrued is **110/512**, so the next **~111 bytes out of `.cold`**
uncross a second 512-byte rung. Item 2 is over half of that on its own, and
two refusals from the `gfx_blit1` set were priced against a `.cold` rung that
was further away when they were written and are worth re-reading now: the
`.row`/`.rowi` fold (~27 `.cold` for +1.3% per row) and the tail-merge
subroutine (~8 `.cold` for ~4%). Both are still the owner's call under the
gfx-cycles policy — only the number on the other side of the trade has moved.

`.lowbss` is at **478/512 with 34 bytes left**, and
`docs/KERNEL-MEMORY.md` now carries the standing note that the
segment-to-`LOW_SEG` escape valve is closed: the next table to try it pays a
whole step, where every migration already in the tree was taken while there
were hundreds of bytes standing.
