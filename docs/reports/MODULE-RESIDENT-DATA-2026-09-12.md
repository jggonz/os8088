# What an on-demand module costs the kernel while it is NOT loaded

**A measurement, not a description.** Taken 2026-09-12 on a four-core cloud
container, `nasm` 2.16.01, at commit `6c91a3a` (`origin/elendilon-next`), on a
tree where `KERN_SIZE` is 110,592 of `KERN_BUDGET` 129,536 and the sections
are `.text` 49,524, `.bss` 6,016, `.cold` 39,264. It is true of that commit
and of no other tree; a later measurement is a new file.

Reproduce it with `python3 tools/os88modcost.py [--small] [--ovl] [--detail]`,
which is the instrument this report was written with and which re-assembles
the kernel rather than reading a build.

## The question

> Modules — and maybe overlays — keep their data in the kernel, so it is
> taking up RAM the whole time the machine runs and not only while they are
> loaded. Is that true, and how much is it?

## The answer in one line

**It is true of modules, it is written down as the rule rather than being an
oversight, and it is 440 bytes on `kern_big` and 557 on `kern_small` — of
which 329 and 473 could move. It is NOT true of the boot overlay.**

## 1. The premise is the published contract

SPEC.md 2.8.1, last bullet, in as many words:

> A module's **data does not move**. It stays in `.text`, reached through DS
> exactly as cold code's data is, which is what spares this mechanism
> SPEC.md 31.9's string-staging question entirely.

`kernel/ctrl.inc:5482` says the same thing at the point of use, and names the
gate that enforces it:

> `section .text` — DATA, so out of the on-demand image and back to the kernel
> segment (SPEC.md 2.8: a module's data stays in .text, and os88ovlchk.py
> refuses it here otherwise — which is what caught this one)

So the report is not "we found a leak". It is **the size of a stated
trade**, which nobody had measured.

**And half of it has already been paid down.** SPEC.md 2.8.6 opened one door
out — a module may carry its own strings and read them through `CS` — and
SPEC.md 2.8.6.1 records what walked through it: the cloner's prompts (−161
`.text`), the formatter's (−154 `.text`, −84 `.cold`) and the Control Panel's
page strings (−443 `.text`, +28 `.bss`). That is why the number below is
hundreds of bytes rather than kilobytes: **the largest single body of module
data left the kernel two arcs ago.** What is measured here is what did not.

## 2. What is left, measured

A label defined in `.text`, `.bss` or `.cold` whose **every** reference in
the tree is inside a module image section. Anything a resident caller also
names is excluded — that is shared data, and moving it is a design question
rather than an accounting one.

| | `kern_big` | `kern_small` |
|---|---:|---:|
| `DATA` in `.text` — read-only tables and strings | 114 | 184 |
| `STATE` in `.bss` — the module's own variables | 55 | 202 |
| `CODE` in `.text`/`.cold` — bodies only the image calls | 160 | 87 |
| far shims — the ABI out of the image, **cannot move** | 111 | 84 |
| **TOTAL** | **440** | **557** |
| **movable** | **329** | **473** |

Per image:

| image | what it is | `kern_big` | `kern_small` |
|---|---|---:|---:|
| `.modc` | `CTRL.DRV`, the Control Panel (SPEC.md 31.9) | 266 | 129 |
| `.modd` | `FDLG.DRV`, the Standard File dialog (SPEC.md 38.0) | — | 201 |
| `.modp` | `FILECP.DRV`, Cut/Copy/Paste (SPEC.md 22.3) | — | 115 |
| `.modf` | `FORMAT.DRV`, the floppy formatter (SPEC.md 18.96) | 73 | 72 |
| `.modh` | `HIBER.DRV`, hibernate (SPEC.md 87) | 59 | — |
| `.modl` | `CLONE.DRV`, the disk cloner (SPEC.md 18.99) | 41 | 40 |

`.modd` and `.modp` are modules on `kern_small` alone; on `kern_big` those
bodies are resident `.cold` by decision (SPEC.md 2.8). `.modh` is
`kern_big`'s — the 128KB machine has no hard disk to hibernate to. **So the
two columns are different machines and not the same number twice**, and the
larger one is the machine with 128KB in it.

The ten largest rows, `kern_big`:

| bytes | label | sec | image | where |
|---:|---|---|---|---|
| 56 | `dskw_fmt_tab` | `.text` | `.modf`, `.modl` | `kernel/diskw.inc:3916` |
| 40 | `drv_status_x` | `.cold` | `.modc` | `kernel/driver.inc:1324` |
| 28 | `cp_sbuf` | `.bss` | `.modc`, `.modh` | `kernel/ctrl.inc:5513` |
| 20 | `sched_mode_set` | `.text` | `.modc` | `kernel/sched.inc:1407` |
| 12 | `cp_dmbuf` | `.text` | `.modc` | `kernel/ctrl.inc:5498` |
| 11 | `dskw_fmt_jmp` | `.text` | `.modf` | `kernel/diskw.inc:4780` |
| 11 | `dskw_fmt_lab` | `.text` | `.modf` | `kernel/diskw.inc:4785` |
| 11 | `drv_cfgname` | `.text` | `.modc` | `kernel/driver.inc:853` |
| 11 | `drv_sysname` | `.text` | `.modc` | `kernel/driver.inc:854` |
| 10 | `vid_disp_relayout` | `.text` | `.modc` | `kernel/vidsel.inc:2333` |

…and `kern_small`'s head is `dskw_fmt_tab` 56, `fcp_stack` 36 (`.bss`),
`sched_mode_set` 20, `fdlg_row` 18 (`.bss`), `fdlg_tpl` 16, `fdlg_nsave` 16
(`.bss`), `fcp_name` and `fcp_cname` 13 each (`.bss`).

**The top row is a documented refusal and the measurement agreeing with it is
the cross-check worth having.** SPEC.md 2.8.6.1 already names
`dskw_fmt_tab` as the one thing that did not move — *"It is `.text` and stays
there, because `dskw_fmt_row_x` hands callers an `SI` into it and every one
of them dereferences `[si+DFMT_*]`"* — so 56 of the 329 are spoken for before
anybody starts.

### 2.1 A second tier, and it is smaller than it looks

Data shared **only** with the module's own resident half — its thunks and its
greying predicate, in the same file — is a further **62 bytes on `kern_big`
and 45 on `kern_small`**. Most of it is `cp_s_time`, `cp_s_sched`, `cp_s_vid`,
`cp_s_drv` and `cp_s_thm`, and those are SPEC.md 2.8.6.1's own exclusion:
*"all six list names are resident, because a list name may equally be a
driver's staged one and `cp_list` draws it through DS."* So tier 2 is
**not** a second prize; it is mostly the reason tier 1 stops where it does.

## 3. The overlay half of the premise is FALSE

Same instrument, `--ovl`, over `.ovl` and `.ovlw`:

| | bytes |
|---|---:|
| `DATA` in `.text` | **2** |
| `CODE` in `.text`/`.cold` | 492 |
| far shims | 36 |

The two data bytes are `fdd_unit` and `fdd_dbg_ran` (`kernel/disk.inc`). **The
492 bytes of "code" are a false positive and are worth writing down so that
nobody re-derives them as a finding**: they are `sch_isr` (256), `kbm_isr`
(101), `mou_p2_isr` (65) and `sch_idle_body` (30) — an interrupt handler is
*named* exactly once, at the `mov word [es:0x20], sch_isr` that installs it,
and that install is in `.ovlw` because it happens at boot. The code then runs
for the life of the machine. A "named only from the overlay" test cannot tell
that from boot-only code, which is the one thing this instrument cannot see
(see its header).

The genuine article is already gated elsewhere: `tests/ovlrefs.txt` is the
registry of resident routines only the overlay calls, and `dsk_flop_add_x` —
the one row this run pointed at — is in it with a reason.

**So: overlays hold essentially no data hostage.** That is the expected
answer rather than a lucky one: SPEC.md 2.5's overlay is code that runs once
and is handed back by `mem_unblob`, and `.ovlw`'s bodies land on the FAT
window. A boot-time routine writing to a resident `.bss` word is writing to
state the machine then *uses*, which is not the same thing as holding a table
for a feature nobody opened.

## 4. Where the bytes could go, and it is already claimed

`mod_need` sizes a module's claim from the file and rounds **up to whole KB**
(`mem_bytes_kb_x`: `add ax, 1023` / `shr ax, 10`). So every module already
owns memory past the end of its image, for as long as it is loaded and not
one moment longer:

| module | image | claim | **slack** |
|---|---:|---:|---:|
| `CTRL.DRV` (`kern_big`) | 7,542 | 8,192 | **650** |
| `FORMAT.DRV` | 1,129 | 2,048 | **919** |
| `CLONE.DRV` | 5,810 | 6,144 | **334** |
| `HIBER.DRV` | 3,398 | 4,096 | **698** |
| **`kern_big` total** | 17,879 | | **2,601** |
| `CTRL.DRV` (`kern_small`) | 4,709 | 5,120 | **411** |
| `FILECP.DRV` | 2,161 | 3,072 | **911** |
| `FDLG.DRV` | 3,243 | 4,096 | **853** |
| **`kern_small` total** | 17,052 | | **3,428** |

**Every module's own private data fits inside its own claim's existing slack,
several times over** — the largest single item in the whole measurement is 56
bytes and the tightest slack is 334. Moving it costs no heap at all on any
module as they stand today.

And the write is legal: `mov [cs:si], al` assembles under this tree's
`cpu 8086` + `-w+error` and costs one prefix byte, which on an 8088 is the
fetch floor's ~4.3 cycles rather than the ALU's 2 (PERFORMANCE.md Part 2).

## 5. What this does NOT say

* **It is a floor, not a total.** Two references are invisible to a source
  scan and both under-report: a label reached only through a table of
  pointers is named where the table is, and a reference inside a `%macro`
  body is filed where the body is written rather than where it expands.
  `tools/os88ovlchk.py` has the same two blind spots and names them.
* **Movable is not free.** The far shims (111 / 84 bytes) are the ABI a
  module leaves the kernel through and cannot move into the image by
  definition; `dskw_fmt_tab` is a published refusal; and SPEC.md 2.8.6's
  ordering rule — *a string may live in the image only if nothing can try to
  draw it while the image is not loaded* — has to be argued per call site,
  not per string.
* **`.bss` is not one population.** `kernel/filecp.inc:2058` states the
  split in the design's own words: *"What makes an arm safe to drop on is
  that the CLIPBOARD IS `.bss`"* — `fcp_op`, `fcp_drv`, `fcp_cwd`,
  `fcp_type` and `fcp_name`, 19 bytes, are the selection a Copy leaves behind
  for a Paste that will load the image again, and they have to outlive the
  drop. The other ~123 bytes are one operation's own state, alive exactly as
  long as the image is.

## 6. The instrument, and the three ways a listing lies

`tools/os88modcost.py`. Two passes: a **source** scan from `kernel.asm` down
every `%include`, tracking `section`, which classifies; and a **nasm listing**
of the same source, which sizes. The listing sprang three traps on the way to
these numbers and the tool's header carries all three, because each one
produced plausible wrong numbers rather than an error:

1. **nasm lists dead `%if` branches with no marker**, so a `section` directive
   in the branch nasm skipped silently redirects every size after it. Reading
   `filecp.inc` that way filed 2,001 bytes of `kern_big`'s `.cold` as `.modp`.
2. **The source text starts at column 40 on every line** — the `<depth>`
   field at 36 is blank at depth 0 — so slicing at 36 indents every
   `kernel.asm` label out of a `^label:` regex's reach and charges its bytes
   to the label above. `dsk_flop_add`, a six-byte trampoline, read **229**.
3. **A bare `label:` line carries no address at all.** Detect the label
   before the address or the label is skipped and, again, its bytes land on
   the one above it.

So the totals are **checked rather than trusted**: pass B's per-section sums
are compared against `tools/kernsize.py --json`, which re-assembles the kernel
and measures it independently, and a shortfall over 2% exits non-zero. Each
of the three bugs above moved a section total by more than that — trap 2 alone
left `.text` 2,498 bytes short — and the guard was verified by putting trap 2
back and watching it go red.

Ten labels were then checked by hand against their source lines
(`cp_sbuf: resb CP_SBUF` = 28, `fdlg_tpl` = two `dw` rows = 16,
`dskw_fmt_jmp: db 0xEB, 0x3C, 0x90` + `db 'MSDOS5.0'` = 11, …), all exact.
