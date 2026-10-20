# The kernel's stack-balance gate — triage, and what turns it on

**The kernel has never had a push/pop balance gate.** `tools/stkbalance.py` has
defaulted to `apps/` and `kernel/` since it was written, but the *suite row* was
scoped to SHEET, CHART and four shared includes, and the row said why: the
kernel's ISR tails push in one global label and pop in another, so pointing it
at `kernel/` reported noise. It reported **24 findings**.

Pass 1's handoff names the cost of that: *"An imbalance introduced during pass 1
went green through `make`, the fast tier and `stkbalance`."*

This file is the triage of those 24, the two annotations that turn the gate on,
and the row to add when they land. It exists because the annotations belong in
`kernel/sched.inc`, and `kernel/` was owned by the size pass when this work was
done — so the tool change shipped and the two comment lines did not.

**Twenty-three of the 24 were the walker's own model being wrong. One was real,
and it is fixed** (§2).

**`apps/` is a different story and it is already done.** The same walker fixes
took that row from 776 entries to **7,135** — every package, every shared SDK
include and all three CPU cores — and it is green. What stood in the way was
three blind spots, each hiding a whole class of file rather than a routine, and
§6.1 is the account of them. The one real defect this whole exercise found was
there rather than in the kernel.

---

## 1. The result

```
before   24 findings   (0 declared banking, 4 back-edges, 47 no-ret chunks NOT WALKED)
after     3 findings   (3,145 entries walked, 0 capped)
after +   0 findings   with the two annotations in §3
```

The `47 no-ret chunks not walked` is the part worth pausing on. A chunk whose
every exit is a tail `jmp` was not walked **at all** — and that is precisely the
shape tail-merging produces, so the old gate went blindest exactly where a size
pass does its work.

### The triage, all 24

`FP` = the walker was wrong and now is not. `OK` = correct code that needs a
`; STKBALANCE-OK:` because no static walk can follow it. `BUG` = real.

| # | file | routine | reported | verdict | why |
|---|---|---|---|---|---|
| 1 | clock.inc | `clk_at_get` | tail jmp to `$+2` +1 | FP | `jmp short $+2` is an I/O settling delay, not a tail call |
| 2 | clock.inc | `clk_at_done` | tail jmp to `$+2` +3 | FP | same |
| 3 | clockw.inc | `clk_at_put` | tail jmp to `$+2` +2 | FP | same |
| 4 | mouse.inc | `mou_uart` | tail jmp to `$+2` +2 | FP | same |
| 5 | disk.inc | `dsk_put_ico_body` | ret −1 | FP | continuation: reached only by `jmp`, pops its caller's push |
| 6 | diskw.inc | `dskw_wrp` | ret −3 | FP | continuation |
| 7 | driver.inc | `drv_load_row` | ret −8 | FP | continuation |
| 8 | files.inc | `fm_open_body` | ret −2 | FP | continuation |
| 9 | files.inc | `fm_dotin` | ret −1 | FP | continuation, and a **cross-file** one — `fdlg.inc` banks SI and jumps in |
| 10 | mouse.inc | `kbm_move` | ret −3 | FP | continuation (reached by `jcc`) |
| 11 | mouse.inc | `mou_eoi` | ret −9 | FP | continuation — the ISR tail the old row was named after |
| 12 | sched.inc | `sch_resume` | ret −9 | FP | continuation (reached by `jcc` ×5) |
| 13 | ui.inc | `ui_lit_go` | ret −1 | FP | continuation — `ui_lit_on`/`ui_lit_off` both push AX and jump in |
| 14 | wm.inc | `wm_apply_out` | ret −2 | FP | continuation |
| 15 | wm.inc | `wm_cov_o2` | ret −1 | FP | continuation |
| 16 | kernel.asm | `api_x` | ret −1 | FP | continuation |
| 17 | kernel.asm | `api_n` | ret −1 | FP | continuation |
| 18 | vga12.inc | `gfx_disp_enter` | tail jmp +1 | FP | banks AX and tail-jumps to `gfx_dent_act`, which pops it |
| 19 | driver.inc | `drv_pkg_disp` | ret +2 (`retf`) | FP | `push ds` / `push [fptr]` / `retf` is a constructed **far jump**; +2 is the idiom's signature |
| 20 | splash.inc | `spl_isr` | ret +1 (`iret`) | FP | `pushf` + `call far` chains an interrupt: the chained handler's `iret` pops our flags |
| 21 | instance.inc | `inst_pkg_alive` | tail jmp to `task_exit` +2 | OK | the task is being torn down; the stack is abandoned |
| 22 | instance.inc | `inst_pkg_alive` | tail jmp to `inst_task_die` +2 | OK | same |
| 23 | sched.inc | `task_yield` | ret +2 | OK | fabricates an int 08h frame; the `ret` is reached by the scheduler's `iret` |
| 24 | — | `op_size` | (found in `apps/`) | **BUG** | §2 |

Rows 21–23 collapse to **two** annotations, because `task_exit` and
`inst_task_die` both reach their `iret` through `sch_switch`.

**On `retf` and `iret`.** The first guess was that the walker mis-modelled them,
since a far return pops two words and a near one pops one. It does not, and the
check is worth recording: depth is measured **relative to the routine's entry**,
and the far return address and the hardware interrupt frame are both pushed
*before* entry, so they were never in the count. Every `retf`/`iret` row above
has a different cause.

---

## 2. The real one — `op_size` returned into a saved register

**`apps/os88parts.inc`, `op_size`, and it is fixed on the same branch as the
walker.** The multi-segment part sizer (SPEC.md §20.12) has one overflow handler
serving two paths that bank different amounts:

```
.ovfc:
    pop cx          ; gives back exactly one word
    jmp short .bad
.bad:
    stc
    ret
```

| path | banked | at `jc .ovfc` | after `pop cx` | `ret` |
|---|---|---|---|---|
| `.have` (line 402) | CX | +1 | 0 | correct |
| `.xspan` (line **378**) | AX *and* CX | **+2** | **+1** | **returns into the saved AX** |

The `push ax` at line 374 is never given back on the `.xspan` overflow path, so
`ret` takes the banked AX for its return address and jumps to a data value —
the same failure as `ch_legend`, which is the bug this tool was written for.

**Reachability.** The path is `add cx, [si+OP_R_LEN]`-style overflow, taken when
a part's length is within 511 bytes of 64KB. `OP_R_LEN` is a word read out of an
`.O88` part table, and every byte read off a disk in this tree is treated as
hostile — so it is a malformed-input path, not an unreachable one.

The fix is a second exit that gives back both:

```
.ovfx:                          ; the .xspan path banks AX *and* CX
    pop cx
    pop ax
    stc
    ret
```

`apps/os88api.inc` includes `os88parts.inc`, so this is in **every package**;
`make` builds them all and the short `jc` still reaches its new target.

---

## 3. The two comment lines in `kernel/sched.inc` — APPLIED

**Pass 2 applied both and turned the kernel gate on from that commit rather
than at the end of its pass**, which is the stronger call: the gate now covers
the size pass's own changes as they land instead of being switched on over
them. Neither changes a byte of code — both are the `; STKBALANCE-OK:`
mechanism that already exists.

**In `sch_switch`:**

```nasm
    ; STKBALANCE-OK: the context switch. Its iret restores the NEXT task and
    ; returns onto that task's stack, so a depth measured against whatever
    ; entry walked in here is meaningless (SPEC.md 8)
```

This is the one that covers rows 21 and 22: `task_exit` ends `jmp sch_switch`,
and `inst_task_die` reaches it the same way. The walker stops when it *arrives*
at an exempted routine, not only when one is its entry — so one annotation on
the routine that actually abandons the stack covers every path into it.

**In `task_yield`:**

```nasm
    ; STKBALANCE-OK: fabricates the int 08h frame (pushf / push cs /
    ; call .save), so the ret below is reached by the scheduler's iret and not
    ; by .save returning
```

Measured: with these two, `kernel/` reports **0**.

---

## 4. The rows

Two rows, deliberately not one, because they were arrived at from opposite
ends and the file lists have nothing in common:

* **`stkbalance`** — the kernel, plus SHEET, CHART and the four shared includes
  the row already had. Pass 2 widened it with a `_kernel_sources()` helper that
  asserts the list is non-empty, which is the right instinct: a gate reporting
  0 findings because its glob came out empty is indistinguishable from a clean
  tree.
* **`stkapps`** — `tests/unit/t_stkapps.py`, which walks **all of `apps/` and
  all of `drivers/`**: 9,038 entries against the 776 the old row covered
  (§6.1). `drivers/` was in no file list either, so the TCP/IP stack had never
  been walked by anything. It is a test rather
  than a longer file list because `wvm_tab` is generated by
  `tools/weavesim.py --emit-optab` into a target only the on-demand `weave`
  builds make, so the test generates it into a temp dir and a tree that has
  never built Weave still gets the coverage.

The overlap is SHEET and CHART, walked by both, and it is left alone on
purpose — it costs 0.2 s and removing it would mean editing a row whose prose
carries the `ch_legend` history.

The whole-corpus walk is **0.6 s** for the kernel and 1.9 s for `apps/`; the
fast tier runs 12.9 s of a 30 s budget with both rows and the walker's own
fixture in it.

## 5. What changed in the walker

Six model fixes, all in `tools/stkbalance.py`:

1. **Corpus-wide, not per-file.** Shared tails cross files — `fdlg.inc` banks SI
   and jumps into `files.inc`'s `fm_dotin`.
2. **Entries, not chunks.** A global that is only ever the target of a `jmp` or
   `jcc` is a *continuation* and is never walked from zero; it inherits the depth
   of the path that reaches it. Tail jumps are followed. This is rows 5–18 and
   the whole of the 47-chunk blind spot.
3. **`jmp short $+2` is a fallthrough**, not a tail call — the I/O settle.
4. **`pushf` + `call far` is net zero** — chaining an interrupt.
5. **`push`/`push`/`retf` is a constructed far jump** — balanced by construction.
6. **A dispatched jump table is followed.** `jmp [tab + bx]` pushes every arm at
   the dispatcher's current depth, and an arm is not an entry. Without this,
   `wcanvas.asm`'s five-register far entry made each of its arms report −5.

Plus two parsing fixes: a **data directive terminates a walk** (control flow
never runs through a `dw` table — `dbg_reg` used to fall out of the bottom of
one), and **`owner.local`** resolves (`font_char.chok`, nine sites).

---

## 6. Why the result is trustworthy

A gate reporting 0 could just mean it stopped looking, so both halves were
measured on this tree.

**Sensitivity — 42/42 caught**, each on the fully-annotated kernel that reports 0:

* 30/30 deleting a single `push` or `pop` at random from 10,633 candidate sites
* 12/12 cross-jumping one routine's epilogue into another's that pops a
  different number — the `filecp.inc` shape, and 11 of the 12 were cross-file

**Specificity — 12/12 quiet** on balanced edits (a matched `push`/`pop` pair
inserted at random), and 0 findings across 3,145 kernel entries and 7,135 in
`apps/`.

**Regression guard.** `tests/unit/t_stkbalance.py` pins eleven idioms the walker
must stay quiet about and six defect shapes it must catch. Against the walker as
it was, **9 of its 17 checks fail** — and one of those nine is a *LOUD* row: the
old walk skipped a routine whose every exit was a tail jmp, so it could not see
that defect shape at all. The gate got **more** sensitive, not less.

**And a live negative control.** Reverting the one-character `op_size` fix makes
`tests/unit/t_stkapps.py` fail with the finding that found it. A gate whose
failure mode has never been observed is a gate nobody has tested.

### 6.1 `apps/` was three blind spots, not three clean files

Widening the row from 776 entries to 9,038 took closing three gaps, and each hid
a whole class of file rather than a routine:

* **`apps/*/*.inc` was in no file list at all.** The tool's own default globs
  `apps/*.inc` and `apps/*/*.asm`. RunCPM's Z80 core, the C64's 6510 and
  Weave's VM — the three largest bodies of assembly under `apps/` — had never
  been walked by anything.
* **All three dispatch as `jmp [cs:bx+ed_tab]`** — a segment override in front
  and the table *second*. A matcher expecting `jmp [tab + reg]` reads every
  opcode handler as a routine entered at depth 0.
* **`wvm.inc` puts its branches inside macros.** Seven of them wrap
  `je %%o / jmp %1 / %%o:` and are used at fifty sites; read as bare mentions,
  every target looks like an address being taken.

`drivers/` was the fourth of the same kind — in no file list, never walked, and
it came out at two findings, both of them `drivers/net`'s `hd_path` pushing one
handle per path level and giving them back with `loop` on either unwind. The
count lives in CX, so no static walk can pair them, and the two unwinds meet on
a **forward** edge where the back-edge suppression does not reach.

Two routines are exempt, and the second is the shape worth knowing:
**`wvm_exit`** is Weave's VM unwind, and every refusal funnels through it to
`_wvm_slice.out`, which gives back the registers the *slice* banked rather than
the ones the opcode handler that refused did. It is `sch_switch`'s shape one
layer out, and it carries the same `; STKBALANCE-OK:`.

## 7. What is still not covered

Stated so it is visible rather than assumed, and all of it is in the tool's own
summary line:

* **Loop back-edge conflicts are suppressed** — but only when the edge stays
  inside one routine. That qualifier is not a detail: the rule first said
  "backward, same file", and **a tail merge is a backward cross-jump**, so the
  gate was blind to the commonest shape a size pass creates. Size pass 2's
  adverse review found it by reasoning and could not build a case that stayed
  silent; `tests/unit/t_stkbalance.py`'s `a BACKWARD tail-merge` fixture is
  that case, and it needs three things at once — the two depths must meet at a
  label rather than a `ret`, the target must be earlier in the file and owned
  by another routine, and the balanced path must be walked first. Without the
  owner test it reports **0 findings** and the only trace is the back-edge
  counter moving to 1. What is left suppressed is the genuine article: a loop
  that pushes N and a second that pops N, with the count in a register, which
  no static walk can pair. 4 in the kernel, 71 corpus-wide.
* **A computed jump through anything but a recognised `dw` table** ends the walk.
* **`STKBALANCE-OK` is a full stop.** An exempted routine is not walked and is
  not walked *into*. That is what makes two annotations cover five paths, and it
  is also the mechanism's whole risk: the reason in the comment is the only thing
  standing behind it.
* **Depth is words, and only `push`/`pop`/`pusha`/`popa`/`add`/`sub sp, imm`
  move it.** A routine that moves SP by arithmetic through another register is
  invisible.
* **A corpus spans several translation units and one name can be defined
  twice** — `apps/cc/crt0.asm`'s C runtime is copied into Loom's PV module,
  `drivers/net` and `drivers/ether` share `eth_*` state names. The first
  definition wins. It is 22 of 9,786 across `apps/` and `drivers/`, all of them
  deliberate copies of the same code, and the count is printed so the
  inaccuracy is visible rather than assumed away.
* **`boot/` is not gated.** It walks clean today (50 entries, 0 findings) and
  is left out on purpose: four separate flat binaries that define some of the
  same names, so unioning them is exactly the case in the line above, for
  almost no code.
