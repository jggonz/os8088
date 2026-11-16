# Hibernate resume: no question, Shift to skip, and a boot that does only what the resume needs

> **STATUS (2026-11-11): (A) AND B2 (waves B1, B2) BUILT, not yet run on the
> emulator rows or the iron; B3 and B5 not taken, B4 taken separately
> (docs/plans/B4-WAKE-PLAN.md)** — see §5 for the
> owner's decisions and what landed, commit by commit. The text below is the
> proposal as researched on `117cd5b8`, kept as written except where §5 says
> a decision overrode it.
>
> **PROPOSED** (as researched). Two independent changes to SPEC.md
> §87.5's resume, each with its own recommendation: **(A)** the Resume/Discard
> window goes, the boot resumes by itself, and **Shift held at startup** skips
> it the way it skips `\AUTOSTRT` (§26.10.5); **(B)** the boot stops building a
> whole desktop it is about to throw away and resumes from inside `kmain_o`,
> after only what the resume needs. (A) is small and stands alone; (B) is
> staged and is better done after (A), because (A) removes the one thing
> (B) cannot do early: ask a question on a desktop.
>
> **Figures.** Every time below is **PREDICTED** unless it says MEASURED, and
> every MEASURED figure names where it came from. Byte figures are
> **ESTIMATED** from the source (instruction counts, string lengths) unless
> marked MEASURED off `build/kernel.map` of this tree (base `117cd5b8`). Per
> CLAUDE.md and docs/plans/KERN-DOS-PLAN.md's correction, **a hard-disk timing
> off MartyPC is not quotable** (its ATA model has no mechanics), so the
> hard-disk numbers here lean on the FIELD figures and are predictions.
>
> **Owner's words (2026-11-10):** *"Those dialogs are not super helpful; if the
> user is restarting with a hibernation file, they almost certainly intend to
> resume (or can easily close some windows if they really didn't). The only
> reason to say 'no' is to get out of an error loop; so lets instead research
> removing the dialog, and having shift held down during startup bypass it
> (like it bypasses autostarts). Additionally, right now we fully load
> drivers, fully make it to the desktop, and then immediately discard that
> work and redo it to load hibernation. Lets consider working more like the
> dos live resume - loading only what is needed to resume the hibernation
> file, if there is one, and then proceeding to do so."*

## 0. Sources

- SPEC.md §87 (all of it; §87.1 the window, §87.3 the two files, §87.5 the
  probe/question/stub, §87.6 the wake, §87.9 kern_mega), §96.49 (the live
  resume, `kerndos/kdresume.inc`), §26.10.5 (`\AUTOSTRT` and its Shift),
  §2.5.3 and §2.9.15 (`.ovl`, the blob, `.boot3`), §15.6 (the status line).
- `kernel/hiber.inc` (`hb_probe_x`, `hbm_ask`, `hbm_onkey`/`hbm_act`,
  `hbm_res`, `hbm_down`, `hbm_handover`, `hbm_wake`), `kernel/hbstub.inc`,
  `kernel/hbstage.inc`, `kernel/kernel.asm` (`kmain_o`, `kmain`),
  `kernel/desksc.inc` (`as_boot`/`AS_WALK`, `stik_boot`), `kernel/driver.inc`
  (`drv_boot_x`), `kernel/ui.inc` (step 0), `kernel/megares.inc`,
  `kerndos/kdresume.inc`.
- docs/reports/big-review-2026-11-08/areas/hiber.md (hiber-1, -7, -8, -9,
  -14, -21 and the prior-art section) and its synthesis row BR-10
  (docs/reports/big-review-2026-11-08/synth/bugs.md).
- Timings: docs/plans/completed/BOOT-PERF-PLAN.md §1.1 (MEASURED phase table
  of a hard-disk boot, 86Box XT "8088VGA", `BOOTPROF=1`);
  docs/FIELD-MACHINES.md (MEASURED on the owner's 5150 with an ST-225 on an
  ST11M: boot from the hard disk **3,240 ms**, from the floppy **9,941 ms**);
  docs/plans/KERN-DOS-PLAN.md (MEASURED on three machines including that
  5150: **~2 s to write and ~2 s to resume** a 640KB image); SPEC.md §96.49
  (MEASURED, MartyPC, the live resume 4.03 guest s against 15.4 by `int 19h`
  — a ratio, since the absolute is XT-IDE's); CLAUDE.md's cost table (an
  `int 13h` call ~400 ms on a floppy).

## 1. Today, from the key to the resumed session

A kern_big machine that hibernated to `C:\HIBERNAT.IMG` is switched on. What
runs, in order, and what of it survives (field 5150, hard-disk boot; floppy
column for a floppy boot whose `SYSTEM.CFG` loads `HDD.DRV`, the `hibernatedrv`
shape). Every row is PREDICTED unless marked.

| # | phase (where) | what it does | HD boot | floppy boot | kept by the resume? |
|---|---|---|---:|---:|---|
| 0 | POST | the ROM | — | — | n/a — nothing here can touch it |
| 1 | stage 1, the blob, the kernel (`boot.asm`/`boothd.asm`, `boot2.asm`) | ~4 to 6 HD calls; ~125 floppy sectors | ~0.5 s | **~7.8 s** (MEASURED shape: BOOT-PERF section 1.1 "boot + early init" 329 ms on 86Box HD; the 5150 floppy boot is 9.94 s whole) | **no** — the image carries the same build's kernel byte for byte (§87.3); only the disk layer and HIBER.DRV's loader were needed |
| 2 | `kmain_o` early: `dsk_boot_from`, `xm_sniff`/`cpu_detect`, `dsk_dpt_init`, `sched_init`, `clk_init`, `vid_init`, `vid_probe_avail`, `cfx_slide`, `mem_init`, the module table | CPU and the RTC ladder | ~0.3 s | ~0.3 s | **partly** — `[mem_top]`, `[vid_kind]`, `.cold`'s rung and the build are what the pointer is checked against; `clk_init` is HS_CLK's source (§87.6 step 2) |
| 3 | `font_init`, `band_init`, `menu_init`, `inst_init` | CPU | ~0.1 s | ~0.1 s | no |
| 4 | `mouse_init` | §9.4.1's reset and identify waits | **~0.6 s** (MEASURED 618 ms, BOOT-PERF section 1.1) | ~0.6 s | **no** — `hbm_wake` step 3d re-arms the IMAGE's mouse (§96.45.2); the live route proves it needs no `mouse_init` |
| 5 | `desk_init` (§18.97's floppy probe), `dock_init`, `files_init`, `drv_init`, `drv_sniff` | CPU and the FDC | ~0.05 s (MEASURED 51 ms; 1.76 s worst case, §15.6.4) | same | no |
| 6 | `drv_boot_x` | mount, `xm_boot`, `SYSTEM.CFG`, every wanted driver attached | **~0.7 s** (MEASURED 734 ms, BOOT-PERF section 1.1) | **~2.0–2.9 s** (MEASURED 1,972–2,758 ms, same table) | **only the transport**: a `DRVC_DISK` row when the image is on a driver volume, `XMEM.DRV` when `.cold` was in the HMA. Every other driver is shut down at step 12 and reloaded by the wake at step 14 |
| 7 | `hb_probe_x` | quiet mount of `hb_pick`'s root, `drv_find` | ~1 call | ~1 call | yes (it is the probe) |
| 8 | `kbm_adopt`, `thm_set`, `spl_finish`, `vx_boot_x`, **`as_boot`** | **AUTOSTART launches every link** | 0 to seconds | 0 to many seconds | **no — and worse**: an autostarted program runs in a session about to be discarded, and may write the disk under the image (hiber-21). `stik_boot` already stands down on a pending post; `as_boot` does not |
| 9 | `kmain`: `mem_unblob`, the first `wm_paint_all`, `cursor_show`, `drv_notice`, `stik_boot` | the desktop | ~0.15–0.3 s (MEASURED 146 ms first paint, 86Box) | same | no |
| 10 | `ui_task` pass 1: `hbf_perform(UI_RBQ_ASK)` | `mod_need` HIBER.DRV (~6.2KB packed), `hbm_ask`: the pointer, the image's size, the geometry, then the window launched and painted | ~3 to 5 calls + a window | ~3 floppy calls (~1 s) + a window | the module, yes; the window, no |
| 11 | **the user** | reads the question, clicks Resume | seconds | seconds | — |
| 12 | `hbm_res` | the image's chain to extents (~1–2 FAT calls), `cpf_cp_flush_close`, **`drv_shutdown`** of step 6's drivers, `vid_reboot`, stage, `hbm_handover` | ~0.2 s | ~0.2 s | the extents, yes |
| 13 | the stub (`hbstub.inc`) | ~50–80 `int 13h` reads, track-sized | **~2 s** (MEASURED, field) | ~2 s | yes |
| 14 | `hbm_wake` | PIT, clock, caches, `vid_setmode` + paint, **delete both files** (one mount, ~4–6 calls), **reload `[hb_drvmask]`'s drivers** (`hbm_reload`) | ~1 s | ~2–3 s (the `.DRV` files are re-read from the floppy) | yes |

**What is thrown away**, then: on the field 5150's hard-disk boot, steps 3, 4,
5, the non-transport half of 6, 8 and 9, the window of 10 and the teardown of
12 — **~1.7–2.2 s PREDICTED** of a ~3.2 s boot (MEASURED whole), plus every
autostarted program, plus the user's click. On a floppy boot it is ~3–4 s of
the drivers and the mouse, and the ~7.8 s kernel read is the larger
unavoidable-looking half (§3.4 is the one approach that avoids it). And
step 14 then loads the drivers a second time.

**The DOS live resume is the counter-example** (§96.49): `kd_resume` mounts the
image's volume, `dskw_stat_x`s `HIBERNAT.IMG`, walks its chain into `HS_EXT`,
stages the shared stub and jumps — the disk layer, a walk and a stub, nothing
else. **4.03 guest s against 15.4** (MEASURED, MartyPC, `tests/kdreturn.py`'s
fixture). Every refusal there falls back to `int 19h`, so the fast path is
never a new way to fail. (B) takes both halves of that: only what is needed,
and a fallback that is the route that already works.

## 2. (A) No question; Shift held at startup skips the resume

### 2.1 Design

`hbm_ask` today ends in two arms: `HBP_DOS` = 1 posts `UI_RBQ_RESUME` with no
window (KERN-DOS-PLAN 8); every other image opens `KIND_HIBER` in
`HB_M_RESUME`. **(A) makes the first arm the only arm.** A valid image is
resumed; the window's RESUME face, its two buttons and the strings go.

**The Shift test goes in `hb_probe_x`** (`.boot3`, `kernel/hiber.inc`), not in
the module: on a hit, before posting `UI_RBQ_ASK`, `int 16h` AH=02 — the ROM's
read of 0040:0017, `as_boot`'s own idiom — and either Shift set takes the
**skip** arm. Three reasons it belongs there:

1. **No module load to say no.** The skip arm needs a rename and a toast, both
   of which `.boot3` reaches already (`COLD_SEG:dwf_dskw_rename`,
   `KERNEL_SEG:cw_toast_show` — `as_boot` uses the latter from the same
   section). Reading 6KB of HIBER.DRV to decline is the cost the owner is
   removing.
2. **One gesture, one meaning.** `as_boot` reads the same byte a moment later
   (it runs after `hb_probe_x` in `kmain_o`), so a user holding Shift through
   the boot gets neither the old session nor the autostart — a "plain boot",
   which is what somebody escaping a crash loop wants.
3. **It is resident nowhere.** `.boot3` is given back at `mem_unblob_x`
   (§2.9.15); every byte of the skip arm is transient on every kern_big-family
   kernel.

**When it is read.** At `hb_probe_x`, i.e. after `drv_boot` — near the end of
the boot, which is `as_boot`'s argument (§26.10.5): a key held through the
loading screen is a key held now. With (B) the read moves earlier with the
probe (§3.3), and the instruction becomes "hold Shift until the loading
screen's status line says `Loading SYSTEM.CFG`"; both kernels' keyboard ISRs
keep 0040:0017 current, and a held Shift repeats its make code on both the
83-key and the AT keyboard, so a Shift pressed before POST's keyboard reset is
still seen.

**`as_boot` stands down when a resume is posted**, as `stik_boot` already does
(`cmp byte [ui_rebootq], 0 / jne .out`, §110.4's sentence for the same
reason: *that image IS the session*). Today autostart launches into a session
the resume is about to overwrite and can write the disk under the image
(hiber-21); with no question, there is no "Discard" after which autostart
was wanted. A refused image (other build, unreadable) then costs that one
boot its autostart — the pointer is deleted by the refusal, so the next boot
autostarts as normal. Recorded rather than engineered around.

### 2.2 What happens to the files on a Shift skip — three options

The case that decides it is the **error loop**: an image whose resume dies
*between* the handover and `hbm_wake` step 5. After step 5 there is no loop —
the wake deletes both files before it reloads a driver or returns into the
session (§87.6), so a session that crashes later boots ordinarily next time.
Before step 5 the files are still there: a sector the stub cannot read after
its retries prints its sentence, waits for a key and `int 19h`s (§87.5) **into
the same resume, forever**; a hang in `vid_setmode` or the HMA copy-back does
the same with a power switch. Today that loop is escaped with Discard; under
(A) with Shift.

| | **keep** (skip this boot only) | **set aside** (rename the pointer) | **delete** (`hbm_forget`) |
|---|---|---|---|
| on disk after a Shift boot | both files unchanged | `HIBERNAT.PTR` → `HIBERNAT.OFF`; the image unchanged | both gone |
| the error loop | escaped for **one** boot; Shift on **every** boot until the user finds and deletes the files in a Disk window | escaped **for good** with one Shift boot | escaped for good |
| a good image, Shift held by mistake | resumes on the next boot | recoverable: rename `HIBERNAT.OFF` back to `HIBERNAT.PTR` in a Disk window, restart | **lost** — the thing the brief forbids |
| the stale-disk hazard (the session after a skip writes the disk; then the old image comes back over it — hiber-21 magnified: a package holding a file open across the gap writes through a stale view) | **every unshifted boot walks into it**, silently | only by the user's own explicit rename | none |
| prior art | — | Windows 2000+ rewrites `hiberfil.sys`'s signature before it resumes so a failure cannot re-enter it (hiber.md prior art); this is that idea moved to the skip | Linux swsusp invalidates its image when the system boots without resuming; Windows' failed-resume screen offers "Delete restoration data" |
| cost | 0 bytes beyond the test | +~30 `.boot3` (transient) for the rename and the name; HIBER.DRV +~10 so `hbm_forget` deletes a parked `HIBERNAT.OFF` too (else a later manual rename could pair an old pointer's wake address with a new image of the same build) | +~6 `.boot3` and a module load (the delete is `hbm_forget`'s) |

**Recommendation: SET ASIDE.** It is the only one that both ends the loop with
one Shift boot and destroys nothing. It also closes the stale-disk hazard the
"keep" option would open on every later boot: a skipped image never comes back
by itself, only by a deliberate rename the SPEC will describe with its
warning. The toast is `TOASTSTR hb_s_skip, 'Resume skipped (Shift)'` (22 of
24, §59.10.1). The parked image is reclaimed by the next Hibernate, whose step
3 `hbm_forget` deletes `HIBERNAT.IMG` and — new — `HIBERNAT.OFF`.

**Considered and not recommended now: arm-once** (rename the pointer to `.OFF`
in `hbm_res` *before* the handover, and let the wake's step 5 delete `.OFF`
instead of `.PTR`). It makes the loop impossible with no Shift at all — a
crashing image is tried exactly once — at ~+12 HIBER.DRV bytes. Against it:
a disk write ahead of every resume (one directory sector, ~1–2 `int 13h`), and
it gives up the property §87.9 states on purpose — *"no disk write precedes the
stub, so a power cut during a good resume still finds its image"* — for one
where a power cut sets a good image aside. The owner asked for Shift as the
escape; arm-once is the owner's call and is recorded here as a cheap follow-up
if the field ever shows a loop that users cannot get out of.

**One cheap addition worth taking with (A):** the stub's own failure sentence
(`hbstub.inc`, printed when a read fails three times) gains *"Hold Shift while
it restarts to skip it."* That is the one place a loop is visible to the user,
and it tells them the escape. **But** `hbstub.inc` is shared as source with
`kern_dos` (§96.49), whose image had 392 bytes left on its rung when the live
resume landed — so the sentence is ~+35 bytes of HIBER.DRV's staged stub and
of `kern_dos`'s image both, and needs `KD_IMG_KB` re-checked (the owner may
prefer a shorter `Shift skips it`).

### 2.3 Touch points

| file | change |
|---|---|
| `kernel/hiber.inc` `hb_probe_x` (`.boot3`) | on a hit: Shift → rename `HIBERNAT.PTR` → `HIBERNAT.OFF` (the new name staged into DS, as `as_boot` stages its folder's), toast, post nothing; else post `UI_RBQ_ASK` as today |
| `kernel/hiber.inc` `hbm_ask` | `.askit`'s `hbm_kbdrain` + `cw_app_launch(KIND_HIBER)` replaced by the DOS arm's `hbm_post(UI_RBQ_RESUME)`; the `HBP_DOS` test becomes "read the exit code if set" only. kern_mega's `MRF1` check stays where it is |
| `kernel/hiber.inc` `hbm_act`, `hbm_paint`, `hbm_btnrect`, strings | the RESUME face goes: `hbm_s_r1..r3`, `hbm_lines_res`, `hbm_s_bres`/`hbm_s_bdis`, the `.resume`/`.discard` arm, the letter patch into `hbm_s_r1`, the RESUME offset in `hbm_btnrect`. `HB_M_RESUME` stays as a MODE (`hbm_res`'s refusal sets it and `hbm_winback` reads it) or is renamed; the face does not. `hbm_kbdrain` and its call go (it existed so a key repeating through `int 19h` did not answer the question; with no question there is nothing to answer, and keys typed during the boot are in the fresh kernel's ring, which the stub overwrites). The ASK face's third line `Restarting offers to resume it.` becomes `Restarting resumes it.` (hiber-7/BR-10's whole class is gone: there is no Esc on a Resume face) |
| `kernel/hiber.inc` `hbm_res` `.fail`, kern_mega `.refuse` | **hiber-8 becomes mandatory**: every resume is now a no-question resume, so a refusal must set `HB_M_GONE` when there is no window (`call hbm_win / jnc`), or HIBER.DRV stays pinned for the session (+~8) |
| `kernel/hiber.inc` `hbm_ask` | **hiber-9 with it** (+~6): a `FERR_BIG` read (a kern_mega pointer met by kern_big) goes to `.stale` and is forgotten, instead of being re-loaded silently on every boot |
| `kernel/hiber.inc` `hbm_forget` | also deletes `HIBERNAT.OFF` (+~10, one more stamped extension in `hbm_nset`'s shape) |
| `kernel/desksc.inc` `AS_WALK` | stand down on `[ui_rebootq]` ≠ 0, before the folder test. In `.boot3` on kern_big/kern_emu (transient); in **`.cold` on kern_mega** (`as_bootc`, resident) |
| `kernel/hbstub.inc` (optional, §2.2) | the failure sentence names Shift |
| SPEC.md | §87 *What it is* ("asks, on the desktop" → resumes by itself; Shift); §87.1 (two faces, not three; the RESUME face's history kept as one paragraph, like the file dialog's); §87.3 (`HIBERNAT.OFF`, and that `hbm_forget` deletes it); §87.5 rewritten (probe → Shift → post RESUME; no window; refusals unchanged); §87.7's byte account; §87.8 (the row's second pass); §26.10.5 (one Shift, two skips; autostart stands down under a pending resume); §59 toast list if it enumerates; §96.42/§96.49 only where they say "the question" |

### 2.4 Bytes — PER KERNEL (ESTIMATED; measure the spans when built)

The module today (MEASURED, `build/kernel.map` and `os88mod`'s line at
`117cd5b8`): HIBER.DRV is **7,795 bytes** of image, **6,215** on the disk
packed, 16 sectors. What (A) deletes from it, MEASURED as symbol spans: the
RESUME face's three lines `hbm_s_r1`..`hbm_s_bhib` **96**, the `Resume`/`Discard`
labels **18**, `hbm_lines_res` **8**, `hbm_kbdrain` **15**, `hbm_act`'s
`.resume`/`.discard` arms **28** — **165 bytes**, plus the RESUME branches in
`hbm_paint` and `hbm_btnrect` (~15, ESTIMATED) and 9 off the ASK face's third
line. `hb_probe_x` is 76 bytes of `.boot3` today (MEASURED span).

| kernel | resident | transient (`.boot3`) | HIBER.DRV image |
|---|---:|---:|---:|
| kern_big | **0** | +~40 (Shift, rename, toast) +~7 (`as_boot` stand-down) | **−~189** (above) **+~24** (hiber-8, hiber-9, `.OFF` in `hbm_forget`) = **−~165** net |
| kern_emu | 0 | as kern_big | as kern_big |
| kern_mega (conventional) | **+~7 `.cold`** (`as_bootc`'s stand-down) | +~40 | as kern_big, both arms |
| kern_small | 0 (no hibernate, no autostart) | 0 | — |

**ABI**: none. The pointer file's format does not change (`HIBERNAT.OFF` is the
same 18/28 bytes under another name). No `OSAPI_*` slot, no driver verb.

### 2.5 Risks

- **The user who wanted a fresh start** gets the old session. The owner's
  judgement is that closing windows is cheap; a session holding something
  heavy (a 64KB XMS package on kern_mega, a long DOS program's return) is the
  same few seconds.
- **The DOS arm under Shift.** An image written for a DOS handoff (`HBP_DOS`)
  is skipped and set aside like any other; DOS.O88's exit code in the BDA
  mailbox is simply never read. Correct for a loop; recorded.
- **Two toasts** when Shift is held and `\AUTOSTRT` has links (`Resume skipped
  (Shift)` then `Autostart skipped`). Acceptable; folding them is not worth a
  byte.
- **A resident window kind with no resume face.** `KIND_HIBER` still exists
  for the ASK and BUSY faces, so nothing in §29.3's tables changes.

### 2.6 Rows

Change `tests/hibernate.py` (soak `hibernate`, `hibernatedrv`, `hibernatem`):
the first pass no longer finds a question — the boot resumes by itself and the
row reads `[hb_resumes]` and the About witness as before; **the second pass
(today "Discard by keyboard") becomes "Shift held through the boot"**
(`m.key("ShiftLeft", down=True, up=False)`, `tests/autostart.py`'s technique):
`Resume skipped (Shift)` toasted, `[hb_resumes]` unchanged, on the host
`HIBERNAT.PTR` absent and `HIBERNAT.OFF` and `HIBERNAT.IMG` present; then a
third boot with nothing held is an ordinary desktop. **Red arms**: invert the
Shift test (pass 1 goes red); make the skip `hbm_forget` (the host's `.IMG`
check goes red); remove the `as_boot` stand-down with a fixture
`\AUTOSTRT\CALC.LNK` on the system disk (a Calculator instance in the restored
table that the hibernated session never had). Also `hmahiber`, `p2hiber`,
`kdreturn`/`kdreturnf` (the DOS arm's no-question path is now everybody's),
`megahiber`, `autostart` (C must still pass), `b3poison` (new `.boot3` code
must not be reached after `mem_unblob`), and the fast tier (`toast`,
`ovlchk`, `blobruns`).

## 3. (B) Resume early: build only what the resume needs

### 3.1 What the resume actually needs from the fresh boot

Read off `hbm_ask`/`hbm_res`/`hbm_down`/`hbm_handover` and the stub:

1. **The checks the pointer is compared against** (§87.3): the build
   (`BUILD_NUM`, `MOD_STAMP` — constants), `[mem_top]` (`mem_init`),
   `[vid_kind]` (`vid_init`), `[api_coldseg]`'s high byte (after `cfx_slide`,
   `rom_patch` — and after **`XMEM.DRV`** when the image's `.cold` was in the
   HMA, which only `drv_boot`'s `xm_boot_x` installs), and on kern_mega the
   mode (`mega_enter`, which runs after the first paint).
2. **A heap and the module loader**, for HIBER.DRV and the extent claim.
3. **A mounted volume holding the image** and its transport facts: the boot
   partition's are the kernel's own after `dsk_boot_from_x`; a driver volume's
   come from `DSV_GEOM`, so `HDD.DRV` must be attached.
4. **The fresh clock** for HS_CLK (`clk_init` — before `vid_init` today).
5. **A text page** to stage in, and no kernel ISR left that could write it.

Nothing else: not the font, the menu, the instance table, the desktop, the
dock, the mouse, the non-disk drivers, the settings, the theme, autostart or a
painted frame. `hbm_wake` puts back the image's IVT, re-arms the image's mouse
(step 3d), re-programs the PIT (step 1), sets its own video mode (step 4) and
reloads the image's own drivers (step 6).

### 3.2 Two constraints any early path must honour

- **`.ovl` dies at the FIRST MOUNT** (kernel.asm's section map, §2.5.3,
  `os88ovlchk` rule 2e). Every `OVLCALL` in `kmain_o` — `menu_init`,
  `inst_init`, `desk_init`, `dock_init`, `files_init_x`, `drv_init_x`,
  `drv_sniff` — must have run before anything mounts, *or the ordinary boot
  that follows a probe that finds nothing calls into the FAT window*. The
  probe mounts (`dsk_chdir_q`), and so does `mod_need`.
- **An early probe must DEFER, never DELETE, what it cannot judge yet.**
  `hbm_ask` today deletes a pointer whose head does not match. At an early
  point an HMA-`.cold` image's head *will not match yet* (XMEM.DRV has not
  run), nor a kern_mega PM image's mode. Those must go on to the later probe,
  not be forgotten — a new `hbm_ask` outcome, "not yet".
- **The loading screen animates on IRQ0** (§15.3.8) until `spl_finish`. A
  resume taken from inside the boot must stop it (`spl_finish`, or a quiet
  stop) before `hbm_down`'s `vid_reboot`, or a spinner frame lands in the text
  page the stub is staged in.

### 3.3 The approaches

#### B1 — Resume from `kmain_o` where the probe already is

On a hit (and no Shift), `hb_probe_x` does not post: it stops the splash and
calls `hbf_perform` with a new code that runs `hbm_ask` and, on a valid image,
`hbm_res` straight away — from inside `kmain_o`, on task 0's stack, with
nothing held, which is `ui_task` step 0's context in every respect that
`hbm_perform` asks about (it banks its own SP in `[hb_sp]`). A refusal returns
into `kmain_o`, which carries on with `spl_finish` and the desktop exactly as
now, with the toast posted. `.ovl` is long dead by then and nothing after
`hb_probe_x` calls it.

- **Saves**: steps 8–12's waste — autostart (0 to seconds), the first paint
  (~0.15–0.3 s), the `ui_task` pass, the window, and in `hbm_res` the
  teardown of a desktop (`vid_reboot` of a painted screen). **~0.3–0.5 s
  PREDICTED** plus autostart, on either boot device.
- **Bytes**: `.boot3` +~15 (the splash stop and the call); HIBER.DRV +~10 (an
  entry code that chains ask → res); resident **0**.
- **Risk**: low. The refusal path is today's (toast, delete, carry on).
- **Format/ABI**: none.
- **kern_mega**: its PM images can only be judged after `mega_enter` (§3.2),
  which runs after the first paint, and `ui_task`'s first pass follows it
  directly. Once (A) has removed the window and stood autostart down, an
  earlier call there would save nothing, so kern_mega keeps today's late
  probe: **0 bytes**, no change beyond (A).
- **Weakness**: it is the tail of every better answer and saves least: the
  mouse and the drivers, which are the owner's complaint, are still loaded and
  thrown away.

#### B2 — Probe before the drivers (RECOMMENDED)

Move the probe to **P1, immediately after the last `OVLCALL`** (`drv_sniff`)
and **before `drv_boot_x`**, and move **`mouse_init`** (blob code, `OVBCALL`,
so it survives the mount) from before `desk_init` to after P1. Split
`drv_boot_x` into two passes over the same `SYSTEM.CFG`: **pass 1** mounts,
runs `xm_boot_x` (XMEM.DRV — `.cold` to the HMA), reads the settings and
attaches only the rows whose class §87.4 step 1 does **not** detach
(`DRVC_DISK`, `DRVC_FILE`, `DRVC_FILE2`, `DRVC_CD`, `DRVC_EMS`) — exactly the
drivers a resume can need for transport and the ones the image keeps; **pass
2** attaches the rest. Then:

```
kmain_o:  ... drv_sniff                       (last .ovl call)
          P1: hb_probe   — boot volume fixed? its root; pointer?
               hit, Shift  -> set aside (A), carry on
               hit, valid  -> B1's direct resume (never returns)
               hit, "not yet" (HMA .cold, kern_mega mode) -> remember, carry on
               miss / refused -> carry on (toast on refusal)
          mouse_init                          (moved)
          drv_boot pass 1                     (disk classes, xm_boot)
          P2: hb_probe   — only if P1 missed or deferred: hb_pick now sees
               DRVC_DISK rows and HMA .cold; same outcomes
          drv_boot pass 2                     (sound, NET, pointing, ...)
          ... spl_finish, as_boot (stands down per (A)), kmain ...
```

- **Saves** (PREDICTED, field 5150 HD boot): `mouse_init` **~0.6 s**, the
  non-disk drivers' load and attach (most of `drv_boot`'s ~0.7 s on a
  machine that boots from C:, where pass 1 has nothing to load), the paint and
  everything after it (B1's ~0.3–0.5 s), `hbm_res`'s `drv_shutdown` of drivers
  it no longer has to shut down, autostart — **~1.5–2.0 s of a ~3.2 s boot**,
  so the switch-on-to-session time falls from ~3.2 + ~0.4 + click + ~3 to
  ~1.4 + ~0.3 + ~3 s. On the floppy-booted `hibernatedrv` shape, P2 hits:
  saves `mouse_init` and pass 2 from the floppy (SOUND/NET/etc., ~1–2 s at
  ~400 ms a call) plus B1's — **~2–3 s PREDICTED** of a ~10–13 s boot; the
  kernel read stays.
- **Bytes** (ESTIMATED): `.boot3` +~40–60 (the second probe site, the pass
  filter in `drv_boot_x`, the "deferred" flag), HIBER.DRV +~25 (`hbm_ask`'s
  "not yet" outcome; the ask → res entry shared with B1); **resident 0** on
  kern_big and kern_emu — `hb_probe_x`, `drv_boot_x` and `kmain_o` are all
  `.boot3`. kern_small: **0** (no hibernate; the `mouse_init` move should be
  `%ifdef KERN_BIG` so kern_small's boot order and its rows do not change).
  kern_mega: PM images stay on today's late probe (0 bytes, see B1); a
  kern_mega image of real mode is "not yet" at P1/P2 too, because whether this
  boot will switch is `mega_enter`'s to decide later — so in this round
  kern_mega gains (A) and nothing from B2, at 0 resident bytes.
- **Risk**: medium, all of it in ORDER. (1) `mouse_init` after `desk_init`:
  the §9.4.1 waits and §15.6.4's status lines are order-aware, and
  docs/plans/MOUSE-BOOT-FREEZE-PLAN.md stamps from the first frame — look,
  but nothing reads mouse state before `kbm_adopt`, which stays after
  `drv_boot`. (2) Two driver passes change the order in which claims are made
  on an ordinary boot (disk drivers first), not the rows' order: `drv_tab`
  rows are fixed, `[hb_drvmask]` is a row mask, and `hbm_reload_m` loads "in
  row order exactly as `drv_boot` would" — which it still does. The heap
  layout of a normal boot moves; watch `heapcheck`/`memp`. (3) The
  "not yet" outcome: a P1 that deletes an HMA image is silent data loss, so
  that arm needs its own red row. (4) `os88ovlchk` rule 2e's "first mount"
  marker moves from `drv_boot_x` to P1 — a tool change in the same diff.
- **Format/ABI**: none needed. **Recommended with it: hiber-14** (bank the
  IMAGE's 8259 masks before the picture and restore those at the wake, kern_big
  as kern_mega already does, +~14 module). Today the reboot route wakes with
  the FRESH boot's mask, and a fresh boot that ran `mouse_init` and attached
  drivers has their lines open; B2's fresh boot has opened neither, so B2
  makes the reboot route look like the live route — which already works on
  the image's terms — and hiber-14 makes that the one wake model rather than
  an accident of which lines the fresh boot happened to open (hiber-1 rides
  with it).
- **Interaction with hardware state**: the drivers B2 does not load are the
  ones `hbm_res` would have shut down anyway (`drv_shutdown` → each driver's
  detach: SB DSP reset, NIC stop) and the wake reloads from power-on state as
  today. A serial mouse that the fresh boot never powered (no MCR write before
  the stub) is powered by step 3d's MCR write, as on the live route; a
  Microsoft mouse then sends its `M` identify byte, which 3d's drain may or
  may not catch — the live route has the same exposure and has not shown it;
  if it does, a short drain after the MCR write is the fix, in the module.

#### B3 — Resume from the loader, before the kernel is read (the `winresume` shape)

Stage 2 (`boot2.asm`, or `boothd.asm`'s path only) looks in the boot
partition's root for `HIBERNAT.PTR` with raw `int 13h` reads, compares the
fixed head against constants it is assembled with (the blob is built with the
kernel: `BUILD_NUM`, `MOD_STAMP`; `[mem_top]` re-derived from `int 12h` the
way `mem_init` derives it; the adapter from the splash's own probe), walks
`HIBERNAT.IMG`'s FAT chain itself, stages `hbstub` and jumps — never reading
the kernel at all, since the image carries it.

- **Saves**: everything after the blob. On a hard-disk boot ~0.4–0.6 s more
  than B2 (the kernel read and early init). **On a floppy boot with the image
  on C:, ~7–8 s** — but that is only reachable if the loader can find the
  image's volume without `HDD.DRV`, which it can when the BIOS answers for
  unit 80h (rung 0 requires that anyway) by parsing the MBR and taking the
  partition `HDD.DRV` would have mounted first — a second copy of a decision
  `hb_pick` makes from the volume table.
- **Bytes**: ~+900–1,100 of `.boot2` (MBR + BPB parse ~150, FAT12/16 root scan
  and chain walk ~300, the head compare ~80, `hbstub` ~440 — shared as source
  as `kern_dos` shares it), resident 0 — **but the blob is read by stage 1 on
  every boot**: +2 sectors of blob, which moves `KSIG_OFF`, the canary and
  `t_blobruns`' run model, and on a 360KB floppy can cost a call on every
  boot of every machine, hibernation or not. Putting it in `boothd.asm`'s path
  only (hard-disk boots) gives up the floppy saving that is its whole case.
- **Risk**: high. A third FAT reader (after the kernel's and `kern_dos`'s
  include of it) in the most size-constrained code in the tree; no fresh
  kernel to toast a refusal (it would have to leave a BDA mailbox the way
  `MRF1` does); **the clock**: `clk_init`'s RTC ladder is not in the blob, so
  HS_CLK would have to come from a minimal `int 1Ah` read on an AT and from
  nowhere on a 5150 with a clock card — a regression against today; HMA-`.cold`
  and kern_mega PM images must be refused to the kernel path.
- **Format/ABI**: the pointer would want the volume it describes (unit,
  partition LBA) so the loader need not re-derive `hb_pick` — a pointer
  format change (allowed this round, but HIBER.DRV, `kern_dos` and kern_mega's
  28-byte pointer all move).
- **Verdict**: not now. The one configuration it wins big on (floppy boot,
  BIOS-reachable C:) is the one an installed machine does not use; on a
  hard-disk boot B2 gets within ~0.5 s of it for a fraction of the risk.

#### B4 — The drivers stay in the image; the wake re-arms them

§87.4 step 1 detaches every hardware driver before the picture and §87.6 step
6 loads it again from the system disk — so a resume loads each one TWICE on
today's route (the fresh boot, then the wake) and still once under B2. B4
keeps them loaded across the hibernate and gives each hardware driver a
**`DRVV_WAKE` verb**: "the machine was power-cycled under you; your memory is
intact; put your hardware back" (SB: DSP reset, sample rate, mixer; AdLib:
nothing; NE2000: re-init, and every socket answered as reset; a pointing
driver: its device). The wake calls it per row instead of `hbm_reload`.

- **Saves**: the wake's driver reload — ~0.5 s on a hard-disk boot, **~1–2 s
  on a floppy boot** where the `.DRV` files come off A: (PREDICTED) — and the
  hibernate's detach; it also makes B2's pass 2 something a resume never
  needs in any configuration.
- **Bytes**: +~10–40 per hardware driver (most have the code in their attach
  already and can share it); HIBER.DRV −~40 (`hbm_sweep`/`hbm_reload_m`'s
  hibernate legs; the suspend slot keeps them); resident 0.
- **Risk**: high and per driver; every hardware driver becomes a two-path
  driver. `OSAPI_DRV_SUSPEND` (§51.11) and `kern_dos`'s handoff share the
  sweep today and would not change.
- **Format/ABI**: **a driver ABI change** — a verb in `drivers/os88drv.inc`,
  `DRV_VER` (14, unreleased this round, so no second bump: re-record
  `tools/abilock.py`). ~~Old drivers answer "unknown verb" and are detached as
  today, so it can land a driver at a time.~~ **CORRECTED (2026-11-11): that
  is false.** ETHER, PCIETHER, NET and USBMOUSE answer CF = 0 to a verb they
  do not know (`ethdrv.inc` ~88-96, `net.asm` ~230-238, `usbmouse.asm`
  ~131-132), so a driver's ANSWER cannot say whether it implements the verb.
  Any future opt-in must be DECLARED - a header flag - and never inferred from
  the answer. Since `9a3a6b67` every driver refuses a verb it does not
  implement (SPEC.md §51.2.2, gated by `drvverb`), so a CF = 0 is now an
  honest yes; the header flag is still the cheaper, safer declaration.
- **Verdict**: a real saving on the floppy-booted machine, orthogonal to B2,
  and the larger change. Record as the follow-up after B2; not this round.

#### B5 — Boot `kern_dos`'s resumer instead of the kernel

`kerndos/kdresume.inc` already is "only what is needed": mount, `dskw_stat_x`,
the walk, the shared stub, and `int 19h` on any refusal. A boot could load a
small `HIBRES` binary built from `disk.inc` + `kdresume.inc` in place of the
kernel's tail when a pointer exists.

- **Needs** the loader to know a pointer exists (B3's raw peek) and a way for
  the `int 19h` fallback not to loop back into the resumer (a BDA mailbox
  "declined" word, `MRF1`'s shape) — and the resumer still pays the clock
  problem of B3, since it has no RTC ladder either (`kern_dos` has none: it
  carries the KERNEL's clock in its launch block, which a cold boot does not
  have).
- **Verdict**: B3's cost with a second binary on every system disk; it reuses
  the right source but not in the right place. Recorded for the lesson: the
  live resume is cheap because it runs where a whole kernel has just handed
  it the facts (`KDL_*`), and a cold boot has nobody to hand it them.

### 3.4 Comparison

| | saves, HD boot (PREDICTED) | saves, floppy boot w/ HDD.DRV (PREDICTED) | resident bytes (big / emu / mega / small) | transient | ABI / format | risk |
|---|---:|---:|---|---|---|---|
| B1 | ~0.3–0.5 s + autostart | same | 0 / 0 / 0 / 0 | `.boot3` +~15, module +~10 | none | low |
| **B2** | **~1.5–2.0 s** + autostart | **~2–3 s** | **0 / 0 / 0 / 0** | `.boot3` +~40–60, module +~25 (+~14 hiber-14) | none | medium (order) |
| B3 | B2 + ~0.5 s | B2 + ~7 s (BIOS-reachable C: only) | 0 everywhere | `.boot2` +~1,000, read on every boot | pointer gains the volume | high |
| B4 | +~0.5 s on top of any | +~1–2 s on top of any | 0 | drivers +~10–40 each | driver verb (`DRV_VER` this round) | high |
| B5 | ~B3 | ~B3 | 0 | a second binary + B3's peek | BDA mailbox | high |

### 3.5 Recommendation for (B)

**B2, built as two waves, with hiber-14 in the second.** It is the owner's
description almost word for word — load only what the resume needs, then
resume — inside the boot that already exists, at **0 resident bytes** on
kern_big and kern_emu, with today's route as the fallback for every case it
cannot judge (a "not yet" carries on to the next probe, a refusal carries on
to the desktop with its toast). It takes the two phases the owner named — the
drivers and the trip to the desktop — and the mouse waits besides.

- **Wave B-1 (B1)**: the direct resume from `hb_probe_x`'s current position
  (splash stop, ask → res chained, no `ui_task` pass). Small, and it is the
  mechanism wave 2 calls from earlier.
- **Wave B-2**: P1 before `drv_boot`, `mouse_init` after it (kern_big
  family only), `drv_boot_x`'s two passes and P2, the "not yet" outcome,
  `os88ovlchk` rule 2e's marker, hiber-14's image masks.
- **Later, separately**: B4 if the floppy-booted resume's wake reload proves
  to matter on the iron; kern_mega's PM images at P1 (they would need
  `hbm_xres`'s checks against a pool layout that `mega_enter` has not built
  yet — `megares.inc`'s stub itself already runs in real mode).

**Not B3** this round: its prize is the floppy kernel read on a machine whose
image is on a hard disk the BIOS reaches — exactly the machine that would
rather boot from that hard disk.

### 3.6 SPEC changes for B2

§87.5 (the probe's two points, the "not yet" outcome, resume from `kmain_o`
without a desktop); §87.6 (what the fresh boot no longer did, hence the image's
masks — hiber-14); §2.5.3 / §2.9.15 (the first mount moves to P1; `.ovl`'s
deadline); §9.4 / §15.6.4 (`mouse_init`'s new place on kern_big); §51 (`drv_boot`
in two passes, which classes are in pass 1 and why it is §87.4's keep-set);
§26.10.5 (unchanged by B2 beyond (A)'s stand-down); §15.6 (a status line
`Resuming C:\HIBERNAT.IMG` in place of `Loading Driver n/N` when P1/P2 hits —
one more `ovl_spl_msg_*` entry in the blob, +~25 `.boot2` transient, optional).

### 3.7 Rows for B2

`hibernate`, `hibernatedrv` (P2), `hibernatem` (mono staging from the
loading screen), `hmahiber` (the "not yet" arm: an HMA image must survive P1
and resume at P2 — **its red arm is P1 deleting it**), `p2hiber` (the PS/2
mouse with no fresh `mouse_init`, and hiber-14's masks), `kdreturn`/
`kdreturnf` (the DOS arm's reboot route lands in the new path), `megahiber`
(kern_mega unchanged), `autostart`, `stikboot`, `bootstatus` (status lines and
the moved `mouse_init`), `b3poison`, `heapcheck`/`memp` (the heap after a
two-pass `drv_boot`), and the fast tier's `ovlchk`, `blobruns`, `canary`. A new
measurement for the final pass: `tools/os88boot.py` on a floppy-booted
MartyPC 5150 (floppy timing is modelled) from reset to `hbm_wake`, before and
after — the one number here that MartyPC can take honestly; the hard-disk
saving waits for the field (docs/FIELD-MACHINES.md).

## 4. Open decisions for the owner

1. **(A)'s file disposition**: set aside (recommended), keep, or delete
   (§2.2).
2. **Arm-once** as well as Shift (§2.2): automatic loop escape, at the price
   of a write before every resume and of §87.9's "a power cut during a good
   resume still finds its image".
3. **The stub's sentence naming Shift** (§2.2), which costs `kern_dos`'s image
   ~35 bytes too.
4. **(B)**: B2 in two waves (recommended), and whether B4 is wanted next.

## 5. The owner's decisions, and what landed

**Decided 2026-11-11** (binding on everything above):

1. **(A), with the files DELETED on a Shift skip** — not set aside, not kept
   (§2.2's three options; the recommendation there was "set aside" and is
   overridden). The owner: *"hibernate is a 'state convenience', not a 'way
   to save work'... a 4MB 286 with a 40MB hard drive may not be able to
   afford the extra file. If they skip it, that is about the same as 'it was
   corrupt' or 'it failed to load' or any other hibernate resume failure.
   This again tracks with how other OSes treat the concept."* So the skip
   takes the same path as any other resume failure, and `HIBERNAT.OFF`, its
   `hbm_forget` leg and the rename are dropped entirely. **No arm-once**
   (open decision 2: not taken). **No Shift sentence in the stub** (open
   decision 3: `hbstub.inc` is `kern_dos`'s too, and it was not asked for).
   The must-ride-along items are taken: hiber-8 (a refused no-question resume
   is spent and `HB_M_GONE`, the module dropped), hiber-9 (`FERR_BIG` is
   `other build` and forgotten), and `as_boot`'s stand-down under a pending
   post (hiber-21's autostart half). BR-10 disappears with the face.
2. **(B): B2, in two waves (B1 then B2), for the HARD-DISK boot.** *"a normal
   user with a hard drive installs there, and thus is resuming from the hdd.
   The kernel read is under half a second and so skipping it is not worth
   the extra work (B3), where skipping the slow mouse/ether/etc's first
   discarded runs is."* Floppy resume must keep working. kern_mega keeps
   today's late probe and gets (A) only. hiber-14 (the image's interrupt
   masks) rides with B2.
3. **B4 is NOT taken this round** (open decision 4's second half). The DOS
   return's reload may bring it back later, and a separate investigation is
   running on that; the driver-pass split below is kept compatible with it
   (the two passes are the keep-set and the rest, which is exactly B4's
   line).
   **B4 HAS SINCE BEEN TAKEN, as its own batch**: docs/plans/B4-WAKE-PLAN.md
   is the costing, the owner's scope (ETHER, PCIETHER, the PicoMEM's USB
   mouse, HTTPFS, VMMOUSE; SOUND deferred) and what landed - `DRVV_SLEEP`/
   `DRVV_WAKE` and `OS88_DRV_WAKES` (SPEC.md 51.2.5), the lease kept across
   a wake (72.5.3), and the wake's drivers back before the paint with the
   rest behind the busy pointer (87.6 steps 4b-6).

**What landed**

- **(A)** — `hb_probe_x` reads Shift and, held, deletes both files itself
  (the kernel's one name, the image's three bytes stamped into it) and toasts
  `Hibernation skipped`; otherwise it posts `UI_RBQ_ASK` as before, and
  `hbm_ask` posts `UI_RBQ_RESUME` for every valid image (the DOS arm's old
  path is everybody's). The RESUME face, its strings, `hbm_act`'s two arms
  and the boot's keyboard drain went; the ASK face says `Restarting resumes
  it.` `hbm_res`'s refusal (and kern_mega's `hbm_xres` `.refuse`) now joins
  `hbm_ask`'s: toast, both files deleted, `HB_M_GONE`. `AS_WALK` stands down
  on `[ui_rebootq]` ≠ 0. MEASURED, kern_big against `2cf57256`: resident
  **0**, `.boot3` **+80**, HIBER.DRV image **7,741 -> 7,524 (-217)**, 6,167 ->
  6,013 bytes on the disk packed. Rows rewritten: `hibernate`/`hibernatedrv`/
  `hibernatem` (the Discard pass is a Shift pass with red arms in its
  header), `hibsumok`/`hibsumbad`, `hmahiber`, `p2hiber`, `win16hiber`,
  `megahiber`, `megap2hiber` (no click: the resume comes by itself).
- **B1** — on kern_big and kern_emu `hb_probe_x` no longer leaves its post
  for `ui_task`: it spends it there and then (`hb_spend`: read and cleared,
  then `hbf_perform`), and on `UI_RBQ_RESUME` calls `spl_finish` and spends
  that too, so a valid image is resumed from inside `kmain_o` with no first
  paint, no autostart and no `ui_task` pass. No new module entry was needed:
  the chain is two calls of the existing perform entry, and the module is
  unchanged. A refusal comes back with nothing posted, so the boot carries on
  as one with no hibernation, autostart included (better than §2.1's
  recorded cost). kern_mega keeps the post. MEASURED, kern_big: resident
  **0**, `.boot3` **+32** (6,190 -> 6,222), HIBER.DRV **0**. `stikhiber` is
  kern_mega's row for the stand-down now; on kern_big its bogus pointer is
  refused before `stik_boot` and the row asserts the relaunch instead.
- **B2** — kern_big and kern_emu (`HB_EARLY`, kernel.asm) probe twice. **P1**
  straight after the last `.ovl` call (`snd_init`); then `mouse_init`; then
  `drv_boot_x` as **pass 1** (the mount, `xm_boot_x`, the settings, the
  adapter, and only the `HB_KEEPSET` classes: disk, file, file2, CD, EMS -
  §87.4 step 1's keep-set); then **P2** unless P1 settled it; then
  `drv_boot2_x`, **pass 2** (everything else). `[hb_down]` carries the probe
  state between the calls (0 / 1 = P1, "early" / 3 = P2 owed / 2 = settled),
  so the two probes cost no resident byte. **"Not yet"**: at P1, `hbm_ask`
  takes a head mismatch or a refused `ROMF_HTAB` as DEFERRED - no toast,
  nothing deleted, the module dropped - because `XMEM.DRV`'s HMA `.cold` and
  SYSTEM.CFG's adapter are pass 1's to set; P2 decides. **hiber-14**:
  `hbm_wrimg` banks the 8259 masks after its sweep (`hbm_imr`, in the module,
  so in the image) and `hbm_wake` step 1 writes them before its `sti` (the
  slave's never on an XT), so a resume whose fresh boot opened no mouse or
  driver line wakes on the image's lines - kern_mega's model on every kernel
  (hiber-1 was NOT taken with it). **The plan's claim that `mouse_init` is
  blob code and survives the mount was wrong for kern_big**: there its
  OVBCALL is an OVLCALL into `.ovl`, so B2 defines `MOU_IN_BLOB` on
  `HB_EARLY` kernels and the probe moves to `.boot3` (kern_small's
  arrangement). `os88ovlchk` rule 2e's marker is the first of `drv_boot_x`
  and `hb_probe_x`. kern_mega is untouched by B2.

**The risks the brief named, and how each is handled**

| risk | handled by |
|---|---|
| the splash/spinner must stop before any text-mode switch | `hb_probe_x` calls `spl_finish` after `hbm_ask` has posted the resume and before `hbm_res` (whose `hbm_down` is the `vid_reboot`); `vid_setmode` is refused while `[spl_live]` stands, so this is required, not tidy. A refusal past it leaves the bar at 100% for the rest of that boot (rare: an unreadable chain, no claim for the extents) |
| `os88ovlchk`'s first-mount rule moving with the probe | the marker regex takes `hb_probe_x` too; the red arm was checked by hand (an `OVLCALL dock_init` after P1 is refused) |
| two driver passes moving where claims land on a normal boot | behaviour unchanged: every wanted row is still loaded by the same `bdl_load` with the same failure handling, `drv_tab` rows and `[hb_drvmask]` are unchanged; only the ORDER of the loads (keep-set first) and so the heap layout move. `Loading Driver n/N` counts in load order. `heapcheck`/`memp` are the rows that see it |
| mouse setup moving after `desk_init` | nothing between reads mouse state (`kbm_adopt` and every pointer probe stay after `drv_boot`); the status lines now read `Looking for Drives` then `Looking for Mouse` on kern_big (`bootstatus` rewritten); `BOOTPROF`'s phase 5 and `MOUDIAG`'s `mdg_n1` now stamp after phase 6 / `mdg_n2` (knob-only) |
| the "not yet" arm deleting an HMA image | P1 defers; but NO row exercises it: `hmahiber` boots from a FLOPPY, so its P1 sees no fixed volume. A hard-disk-booted HMA machine is the row that is owed |

**Bytes, MEASURED against `2cf57256`** (kernsize sections; HIBER.DRV image as
`os88mod` prints it):

| kernel | resident | `.boot3` | `.ovl` | HIBER.DRV image |
|---|---:|---:|---:|---:|
| kern_big | **0** | 6,110 -> 7,486 (+1,376: (A) +80, B1 +32, B2 +142, and +1,122 of `mouse_init` moved out of `.ovl`) | 3,172 -> 2,050 (-1,122) | 7,741 -> 7,565 (**-176**: (A) -217, B2 +41) |
| kern_emu | **0** | 6,206 -> 7,598 (+1,392) | 3,172 -> 2,050 | as kern_big |
| kern_mega | **0** conventional (`.cold` +10, `as_bootc`'s stand-down, above 1MB once switched) | 5,903 -> 5,967 (+64) | 0 | 11,575 -> 11,344 (-231) |
| kern_small | 0 | 0 | 0 | - |

B2's own boot-only cost is +142, over §3.3's +40-60 estimate (the two-pass
state, the probe's state machine and the name's restamp); the plan's
"resident 0" held on every kernel. kern_big's 128KB boot slack (guard 5)
falls 21,504 -> 19,968 because `.boot3`'s lift rises 6,656 -> 8,192 - all
transient.

**Rows** (named, not run here): `hibernate`, `hibernatedrv`, `hibernatem`
(the Shift pass), `hibsumok`, `hibsumbad`, `hmahiber`, `p2hiber`,
`megahiber`, `megahibfull`, `megap2hiber`, `kdreturn`, `kdreturnf` (and the
`kdreturn*` family through the shared wake and hiber-14), `win16hiber`,
`stikhiber` (rewritten for B1), `autostart`, `stikboot`, `bootstatus`
(rewritten), `b3poison`, `heapcheck`, `memp`, `mouresume`, `ps2mouse`, `mouwheel`, the driver boot rows (`hmadrv`, `ethernet`, `drvclaim`,
`drvmem`), `buildmatrix` (the knob kernels' `.boot3` grew),
and the fast tier's `ovlchk`, `blobruns`, `canary`, `toast`, `cswrite`
(green here). Still owed, per §3.7: the `tools/os88boot.py` reset-to-wake
measurement on a floppy-booted MartyPC 5150, and the hard-disk saving on the
field machine.
