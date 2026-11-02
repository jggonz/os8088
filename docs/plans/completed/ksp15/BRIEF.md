# Kernel size pass 15: the brief every agent reads first

Branch `kernel-size-15`, cut from `elendilon-next` at `e4896481`. The pass
before this one was **pass 14**, whose record is the agent notes in
`docs/plans/completed/ksp14/` (it wrote no `HANDOFF-KERNEL-SIZE-P15.md`; its
BRIEF and its notes are the record). The pass before THAT is
`docs/plans/completed/HANDOFF-KERNEL-SIZE-P14.md` (pass 13's record, named for
the pass it hands to).

Each agent works in a git worktree of its own, created by the coordinator at
`/home/user/wt/<name>` on a branch `ksp15-<name>` cut from the commit that
adds this file, and writes its record to `docs/plans/completed/ksp15/<name>.md`.
The coordinator merges the branches; **agents do not push**.

Read before writing any code, in this order:

1. this file;
2. **`docs/plans/completed/ksp14/BRIEF.md` sections 0.2, 1, 2, 3 and 4** -
   the rules, the method, the environment and the note's shape all carry over
   UNCHANGED except where this file says otherwise. Substitute `ksp15` for
   `ksp14` and `(kernel size pass 15)` for the commit suffix everywhere;
3. `docs/plans/completed/HANDOFF-KERNEL-SIZE.md` sections 3-6 (pass 1's
   method and the mistakes that cost it time);
4. `docs/plans/completed/HANDOFF-KERNEL-SIZE-P14.md` section 3 (what the
   owner DECLINED - binding) and 8 (method lessons);
5. **the REFUSED and WANTS THE OWNER lists in `docs/plans/completed/ksp14/*.md`
   and `ksp13/*.md`** for any file you touch - a refused item is not
   re-proposed unless the reason no longer holds, and then the note says what
   changed; **the CROSS-FILE lists in `ksp14/*.md` are this pass's seed list**
   and several are assigned below by name;
6. CLAUDE.md's Hard rules and its banner: **bytes, not rungs**.

## 0. The brief

The owner's words: *"This size pass will work on the recently committed and
not yet size passed kernel work, along with a general pass in an attempt to
recover half a kilobyte. Additionally, set one agent to try to get the
resident, non-mixer variant of sound.drv under 6kb, and as a bonus but not
required, the mixer version to 9kb."* Five agents, all running at once.

**Kernel goal: 512+ bytes off RESIDENT on kern_big** (`.text` + `.bss` +
`.cold` + `.lowbss` + `.vgabuf`, `tools/kernsize.py --json`). kern_small,
kern_emu and kern_mega count too and are reported.

**SOUND.DRV goal**: `snd_keep` (what a machine with no mixer keeps resident,
SPEC.md 34.15.1) from **6,392 to under 6,144** (-249 or more); bonus,
`snd_keepmix` from **9,937 to 9,216 or under** (-721). `tests/unit/t_sndonce.py`
prints both.

**Base** (`e4896481`, assembled, kern_big):

| `.text` | `.bss` | `.cold` | `.lowbss` | `.vgabuf` | **resident** | `.boot2` | `.ovl` | `KERN_SIZE` |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 41,136 | 5,398 | 37,780 | 5,358 | 336 | **90,008** | 6,447 | 5,110 | 97,280 |

Measure kern_small, kern_emu and kern_mega yourself at the base.

### 0.1 What has not had a pass

Pass 14 landed in `a9611731` (#313). Since then, `tools/os88rdiff.py kernel
a9611731 e4896481` reads kern_big `.text` +8, `.cold` +130, `.boot2` +3
(kern_small `.text` +6, `.cold` +40): `ico_grow` 63 (new), `osapi_seg` 40
(new), `mem_rr_ivt` 14 (new), `asc_use_x` +13, `ico_add` +7, `ico_need` +6,
`apic_thm_desk` +6, `ico_rowoff` +3, `ico_max`/`ico_kb`, `mem_rr_tab` -14,
`asc_learn` -3, `vid_seg`/`vid_tab_end`. **The rest of #318 and of
`f8391471` is kern_mega's** (`kernel/mega.inc`, `megares.inc`, `megados.inc`,
`memory.inc`'s `%ifdef KERN_MEGA` arms, the kern_mega memory refactor): that
kernel has never had a size pass, and it is the `recent` agent's.

## 1. The five agents and what each owns

| agent | owns | the work |
|---|---|---|
| **snd** | `drivers/sound/` (and `drivers/os88drv.inc` only if a kernel<->driver change is unavoidable - then it says so first) | SOUND.DRV's `snd_keep` under 6,144, then `snd_keepmix` toward 9,216. Bytes below `snd_keep` are what counts; moving code PAST `snd_keepmix` (attach-only, SPEC.md 34.15.1) or between the two runs (mixer-only) is the structural lever, and `t_sndonce.py` is the gate for every move. The kernel is not this agent's. The image's KB claims, `NOPICOMEM=1`'s figures and every other includer of a `drivers/sound/*.inc` (MIDIRack, RealDMG, `tests/picomem/`, `tests/vidbench/`, the Video Player's speaker library - grep `-I drivers/sound/` in the Makefile) must still build and behave; say what each one's image did. Rows: grep `tests/suite.py` for `snd`, `sb`, `opl`, `mpu`, `covox`, `picomem`, `mixer`, `waverack`, `recorder`, `midirack`. |
| **recent** | the unpassed work of 0.1: `kernel/icons.inc` (and `ico_*` in `disk.inc`), `kernel/assoc.inc`, `osapi_seg`, `mem_rr_*` in `memory.inc`, and every `%ifdef KERN_MEGA` arm (`mega.inc`, `megares.inc`, `megados.inc`, `megapark.inc`, `memory.inc`'s mega arms) | First the kern_big/kern_small hunks of 0.1. Then **ksp14 `nopin.md` CROSS-FILE: every kernel-tag relocation proc of the `cmp bx,[w]`/`jne`/`mov [w],dx`/`ret` shape as a `mem_rr_tab` row** (`asc_reloc`, `dsk_rah_reloc`, `ico_reloc`, `menu_reloc`, `clip_reloc`, `sc_reloc`) - this agent owns exactly those procs in other agents' files, and the free/shed-path reading the caveat demands. Then kern_mega: measure its resident (`.text`+`.bss`+`.cold`+`.lowbss`+`.vgabuf`+`.mega`) and take what a first pass on it finds, `%ifdef KERN_MEGA`-only changes first (they cannot move kern_big). kern_mega's instruments are Bochs and 86Box (docs/BOCHS.md, docs/86BOX.md, `tests/megaport.txt`). |
| **blob** | the "where code lives" moves of ksp14 `reach.md` CROSS-FILE and `api.md` CROSS-FILE 2: `exf_sync` (extmod.inc) into a blob copy + CTRL.DRV copy; `ovc_buf` as nobits (driver.inc); `vgcf_boot` (desksc.inc) into the blob; `desk_rowcalc_x` (desk.inc) and kern_small's `desk_reflow`/`desk_live`; `osapi_drv_dlg_x` into CTRL.DRV behind a refusing thunk; and any further code whose only callers are the blob, `.ovl` or a module (`tools/os88ovlchk.py`, the reference walk - and READ for fall-through entries). Owns `extmod.inc`, `desksc.inc`, `desk.inc`, `ctrl.inc`, `boot2.asm`'s kernel blob blocks and the `drv_boot_x`/`ovc_*` lines of `driver.inc`. The blob is near full: `BOOT2_SECS` before and after, `KSIG_OFF`'s band (`t_canary`) and `t_blobruns` in the note. |
| **gui** | a general finder over the window/drawing side: `wm.inc`, `ui.inc`, `menu.inc`, `mouse.inc`, `cursor.inc`, `font.inc`, `vga12.inc`, `gfx*.inc`, `toast`/`fprog` code, `ctrl`-adjacent resident thunks, `theme`/`win95` resident halves, `sched.inc`, `instance.inc` | The shapes of ksp14 BRIEF 0.2, file by file, largest routines first. Hot paths are cycle-neutral or faster with the count in the note (`hotpar` gates parity). Not `menu_reloc` (recent's). |
| **fsys** | a general finder over the file side: `disk.inc`, `diskw.inc`, `files.inc`, `fdlg.inc`, `filecp.inc`, `loader.inc`, `mod.inc`, `driver.inc` (not blob's lines), `lz.inc`, `compress.inc`, `hiber.inc`'s resident half, `serial.inc`, `kbd*.inc`, `clock`/`rtc` code | The same shapes over these files. Not `dsk_rah_reloc`/`ico_reloc`/`ico_*` (recent's). |

A file not in any row is free to whoever reaches it first: SAY SO in your
note's TAKEN line, and do not edit a routine another agent's row names.
**The parity-pad pairs (ksp14 `api.md` CROSS-FILE 1) are the coordinator's,
at the close** - do not take them; DO list any new pad your change adds.

## 2. What is different from pass 14's rules

* **Five agents at once on 4 cores and 15 GB.** Use `make -j1` (not -j2),
  never run two builds in your own tree at once, and at most ONE MartyPC
  instance of your own. Run your rows ONCE, after your last code commit, as
  pass 14's brief says - and with five agents sharing the box, a row that
  fails on a TIMEOUT is re-run alone before it is believed.
* **ABI**: `main` is `origin/main` at `c2e139eb`. `git diff origin/main
  HEAD -- apps/os88api.inc drivers/os88drv.inc apps/cc/os88.h` is the list of
  what is NOT frozen; everything else in those files is.
* Commit messages end in `(kernel size pass 15)`, and the SOUND.DRV agent's
  too. Each agent's note goes in `docs/plans/completed/ksp15/<name>.md`; run
  `python3 tools/os88index.py` after adding it.
* MartyPC: `mkdir -p build && ln -sfn /home/user/os8088/build/martypc
  build/martypc` in your worktree before your first emulator row - and if
  `/home/user/os8088/build/martypc` is not built yet, ask the coordinator
  rather than building a second copy.
