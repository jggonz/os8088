# Recommendations for things the tree already has

A follow-up to `docs/reports/BIG-REVIEW-2026-11-08.md`, written 2026-11-08 at
the owner's request. The owner noticed that the review lists *"`.LNK` desktop
shortcuts"* when the tree already has them. This file traces that item, then
sweeps every recommendation that ADDS a mechanism, cell, file type, feature,
convention or gate. It covers the report, `synth/phase1.md`, `synth/flagday.md`
and `synth/systemic.md`, and checks each one against the code, SPEC.md,
`tests/unit/`, `tests/*.txt` and `tools/`. It is a measurement of one tree, as
`docs/reports/` always is. Line numbers are this checkout's.

**Verdicts**

- **EXISTS**: the tree already does what the item asks. Withdraw the item, or
  reword it.
- **PARTLY, missed**: part of it exists, and the review does not cite that
  part. The gap it describes is smaller than stated, or its plan leaves out a
  gate that already exists.
- **PARTLY, cited**: part of it exists, and the review builds on that part
  correctly. Nothing to withdraw. These rows are listed so the owner can see
  they were checked.
- **NEW**: nothing in the tree does it.

**The count.** Six items **EXIST**. Four of them are already done, because
the review's own documentation commits (`14428309` round 1, `10912c6e`
round 2) wrote the text that the later synthesis still lists as work to do.
The other two are entries in the *What we would NOT copy* list that describe
as foreign a feature the tree already has. Five items are **PARTLY, missed**.
About twenty are **PARTLY, cited**. The rest, roughly fifty, are **NEW**.

---

## 1. The `.LNK` item, traced

**Where it is.**
- `BIG-REVIEW-2026-11-08.md:598` has the bare bullet *"`.LNK` desktop
  shortcuts."* It sits under **"What we would NOT copy"** (line 578), not
  among the proposals.
- The bullet comes from `synth/systemic.md:1936-1938`: *"Desktop shortcuts as
  `.LNK` files in a `DESKTOP\` folder (Windows 95). Every icon would cost a
  directory walk and a read at boot, ~400 ms each on the XT, where today's
  cached 64-byte picture costs none."*
- No area report and no plan item proposes `.LNK` files.
  `docs/plans/BIG-REVIEW-PLAN.md` does not mention them.

**What it meant.** It refused one storage model: a desktop that *is* a folder
of link files, read back at every boot. It was not refusing shortcuts or links.

**What the tree has.** The tree has both shortcuts and links:
- **Desktop shortcuts**, SPEC.md 26.8 (`SPEC.md:59142`), in
  `kernel/desksc.inc`. A file or folder dragged out of a Disk window onto the
  bare desktop becomes a shortcut. It is a 128-byte record in one movable heap
  claim. The record holds the path, the caption and the 64-byte picture. The
  records persist in SYSTEM.CFG (26.8.1, `SPEC.md:59170-59187`; 26.8.6).
- **`.LNK` link files**, SPEC.md 26.10 (`SPEC.md:59923-60230`):
  - the 50-byte format, `LNK_SELF` and `tools/os88lnk.py` (26.10.1);
  - `lnk_is`/`lnk_open` caught in `assoc_run_x` (26.10.3), so a desktop
    shortcut to a `.LNK` opens what the link names (`SPEC.md:59999-60010`);
  - the Alt-drop that makes one (26.10.4, `SPEC.md:60037`);
  - `\AUTOSTRT` (26.10.5);
  - `apps/os88link.inc` and the link parameter / `OSAPI_ARG_TEXT`
    (26.10.6-7).
- **DOS's own `.LNK` falls through to DOS.** The content says whose a file is
  (`SPEC.md:60017-60027`).

**Verdict: EXISTS.** The review never says that §26.8 and §26.10 exist. Its
four-word summary bullet reads as if the feature were missing.

**Action.** Delete the bullet, or replace it with: *"A desktop that is a
folder of `.LNK` files (Windows 95). os8088 already has both shortcuts
(SPEC.md 26.8, SYSTEM.CFG records with their picture cached) and `.LNK` links
(SPEC.md 26.10). Only the per-boot directory-walk storage was refused."*

---

## 2. EXISTS: withdraw or reword

| # | the recommendation (where) | what the tree already has | action |
|---|---|---|---|
| E1 | `.LNK` desktop shortcuts, in "would NOT copy" (report:598; systemic.md:1936) | Section 1 above: SPEC.md 26.8 and 26.10, `kernel/desksc.inc`, `tools/os88lnk.py` | Reword as in section 1 |
| E2 | gi-7: "state `pit_now`'s bracket limitation in the include and in SPEC.md 53.2.1" (report:571-572; phase1.md:1364 WP15; phase1.md:1909 WP26; systemic gi-7) | `apps/os88pit.inc:39-43` ("NOT INSIDE AN ARMED FSXF_FASTTICK OR FSXF_RATE BRACKET ...") and `SPEC.md:107020-107022` (53.2.1: "`pit_now` laps inside an armed bracket"). Both came from the review's own commit `10912c6e` | Withdraw. Only SPEC 72.15.1's sentence is still owed |
| E3 | ui-8 / BR-22's text half: "the clipboard is **not** worker-safe" (phase1.md:1900 WP26; phase1.md:86) | `apps/os88api.inc:5684-5689` ("legal from a window callback (or the UI task) only - NOT from a worker (SPEC.md 55.4 ...)") and SPEC 55.4 ("The UI task or a window callback, never a worker"), from commit `14428309`. Also `kernel/clip.inc:36` | Withdraw from WP26 |
| E4 | isr-3 / BR-85's text half: "OSAPI_WM_WAKE from an ISR requires SS = LOW_SEG" (phase1.md WP26 table) | `apps/os88api.inc:6631-6633` ("ISR-safe given SS = LOW_SEG (the event ring is addressed through SS)"), from `14428309` | Withdraw the text row. batch C's `evq_push` code change stays |
| E5 | ctx-8 and theme 1: "`sch_lock` as a nestable Forbid inside which nothing yields, sleeps or takes `gfx_lock`", written into SPEC 7 (report:407-408; systemic.md:281; phase1.md:1531) | `SPEC.md:10721-10736` (SPEC 7) already reads: "It is a nestable count (`inc`/`dec`) ... Under it `task_yield` resumes its caller, so nothing that holds it may wait on another task: a `gfx_lock` another task owns spins for ever, and a `task_sleep` returns at once". From `14428309`. `sch_lock` is a counter at 29 `inc` sites (`kernel/sched.inc:3020`) | Withdraw the Forbid sentence. What is really missing is a rule NUMBER and the lock ORDER (`gfx_lock`, `sch_lock`, `mem_cp_busy`, the module lock) |
| E6 | "Not adopted: a shared-library tier", in "would NOT copy" (report:574-576 and 590; systemic.md:1903-1909; the theme-13 heading "do not build a library") | The tree has one shared library, and it works. The Sticky App library, `apps/stiklib.asm` (SPEC.md 110.15), is assembled once and carried in DESK.DRV's image. It is held once on the heap and far-called by every `STIK_LIB` widget through a magic + version entry table (`STIKLIB_MAGIC`, `STIKLIB_VER`). It is handed out by `OSAPI_WM_DESK`'s LIB verb. The review knows it (shell-1, systemic.md:1156) but never sets it against the refusal | Reword: refuse a GENERAL library tier, and name STIKLIB as the one shape that already pays (a library carried by a module that is resident anyway) |

---

## 3. PARTLY, missed: the review does not cite the existing piece

| # | the recommendation (where) | what already exists | what is really missing |
|---|---|---|---|
| M1 | WP3's new row: "a pointerless desktop driven by arrow keys on a CGA `_gla` machine (no `kbm*` row exists)" (phase1.md:506-507) | `tests/wirefleetnomouse.py`, soak row `wirefleetnomouse` (`tests/suite.py:2699`), puts the probe back to "nobody has spoken". That switches the keyboard mouse ON, and the row drives every gesture with the keypad and Space. `tests/mouerrq.py` covers the keyboard mouse retiring | A row named for the keyboard mouse itself, a CGA leg, and the after-hibernate leg. The claim "no row exists" is wrong |
| M2 | WP6 boot-4: lower and derive `SPL_RESIDENT`, "first audit every first-tick store" (phase1.md WP6). Its Rows list names `canary`, `image`, `diskverify`, `bootsmoke`, `blobruns`, ... | `tests/unit/t_resident.py`, soak row `resident` (`tests/suite.py:1746`), is the existing gate on exactly this window. It fails a `jmp kret*` below `spw_resident_end`, with the size guard `SPL_RES_SIZE > SPL_RESIDENT*512` beside it. The kernel already took one dead-adapter boot on this edge (the `vid_pop8` incident, in its docstring) | Add `resident` to WP6's rows. The new part is the derivation and the audit of DATA stores into sectors 5-8, which `t_resident` does not check |
| M3 | drivers-6: "the IVT scan at driver release (+25 `.cold`)" (report theme 2; systemic.md:385-386) | `mem_rr_ivt` (`kernel/memory.inc:3487-3507`) already lists the IVT rows a driver can own: IRQ0-7 at int 08h-0Fh, IRQ8-15 at int 70h-77h, and int 33h. The region-reloc walk already tests each of those words against a block, to move SOUND.DRV's and USBMOUSE.DRV's vectors with their image | Only the release-time call. It should walk the same table rather than a second hand-written list, which probably costs less than +25 |
| M4 | input-2 / BR-19: "one generic in-service rule: `sch_isr` refuses a switch while any IRQ other than IRQ0 is in service (OCW3 0Bh)" (report theme 2; systemic.md:85) | The OCW3 read of the in-service register is already written, in the heartbeat's stop reading (`kernel/sched.inc:1961-1968`, `khb_pisr`). A NESTED IRQ0 already refuses the switch through `[sch_chbusy]` (`sched.inc:2163`, `2201`, `2285`, `2397`; SPEC 8.5.1) | The test for an IRQ other than IRQ0 inside `sch_isr`'s switch decision. Nesting as such is already handled |
| M5 | WP9 / A4: "a runtime API LEVEL" closing SPEC 20.2.0.1's gap (report theme 9; phase1.md WP9; flagday.md 2.A) | The level and the per-cell table already exist: The Wire's `WIRE_LEVEL` = 7 and `WIRE_CELL_LEVELS` (`tools/os88wire.py:120-137`), checked against each package's calls (`:307`, `:432`). The Wire greys a too-new package (SPEC 20.2.0.1, `SPEC.md:44344-44351`), and `abicompat` (soak) tests the old-kernel case. The review cites The Wire but says "only The Wire's own cell fence guards it", without saying that the computation it proposes is already The Wire's code | The kernel word at 0060:0006 and the load-time check, for a package copied by hand. This also answers the owner's point that "the version check covers it". It does not: SPEC.md:44344 says a NEW package calling a cell an OLD kernel lacks loads at the SAME `PKG_FMT` and far-calls past the table. So the remaining gap is real but narrow (hand-copied packages only). The kernel half is ~8 `.cold` |

---

## 4. PARTLY, cited: built on what exists, nothing to withdraw

| # | item | the existing piece it correctly builds on |
|---|---|---|
| C1 | rt-1 drop-to-cache for modules (WP2) | `mod_drop` frees, and `kernel/mod.inc:955-959` records the pin as "deliberately not built". The review says it overturns that decision |
| C2 | rt-9 purge ranks per module | The rank scale `MEM_PG_TRIV/LOW/MED/HIGH` and eight `MEM_P_*` tags (`kernel/memory.inc:182-448`), gated by `tests/unit/t_pgrank.py` |
| C3 | hm-9 package-purgeable claims (`MEMP_PURGEABLE`) | The shed machinery, the relocation proc and `OSAPI_MEM_FLOOR`, with which a package already chooses which kernel caches it may shed (`os88api.inc:1797-1821`). The package-owned tier itself is new |
| C4 | sched-15 `task_wake(AL=slot)` | `sch_wake_ui` (`kernel/sched.inc:708-734`): the same ISR-safe store with a lost-wakeup guard, for task 0 only. The review prices the fold |
| C5 | dr-5 `OSAPI_FILE_FIND_SEQ` | `dsk_dscan`'s resume-from-slot walk, which `fcp_scan` already uses (`areas/diskread.md:251-270`). Only the cell is new |
| C6 | dw-14 contiguous allocation | The rover first-fit (`kernel/diskw.inc:4686-4738`) and the run engine (`:1627-1633`) already make a file on an unfragmented volume contiguous, written as one transfer. New: a run-sized search on a FRAGMENTED volume |
| C7 | dw-15 defer the free-side flush | `OSAPI_BATCH_BEGIN/END` (`os88api.inc:6090-6091`) and `WSEQF_HELD/KEEP` (`:6173-6182`) |
| C8 | WP7 ".bss arrives zeroed: one rule" | The rule is already the tree's: `tests/unit/t_bsssentinel.py` (SPEC 12.8.5.1: "what lands on those bytes at boot is ... ZERO"; a non-zero resting value may not live in `.bss`). New: the assertion on four kernels, and the dead stores |
| C9 | gfce-1 the fault panel | `sch_diepanel` (`kernel/sched.inc:2702`, the stack-overflow panel) and kern_mega's `mega_fkill` (`kernel/mega.inc:60`, `2411`) |
| C10 | L7 "protected-mode clean" header bit | `OS88_HIGH` (0x80) and `OS88_HIGHMEM` (0x40) in the same class byte are already PM-cleanliness declarations, ratcheted by `tests/unit/t_megahigh.py` (`os88api.inc:4290-4342`). The review calls the new one "a third flag bit" |
| C11 | mc-10 `[mega_kfix]` counter | `MEGATRAP=1`'s #GP census to port E9 and `tools/megatraps.py` (Makefile:1556-1565) |
| C12 | gfce-6 a domain column in `CFG_MAP` | One table drives reader and writer, with a per-key `ver` that already protects a key whose meaning changed (`kernel/driver.inc:873-931`) |
| C13 | engsys-12 `t_sections` | `-w+error` already catches an open nobits section, and the end-of-file sentinel block covers 20 sections (`areas/engsys.md:345-357`) |
| C14 | engsys-2 the heading-drift lock | `tools/checkdocs.py` checks 1 and 3 (missing heading, duplicate number). Title drift is new |
| C15 | engsys-6 a COUNTS table | checkdocs check 4 (prose "N sites in M files" against `tests/textsites.txt`) and `tests/unit/t_stackprose.py` do this for two counts |
| C16 | abi-3, md-5, engsys-11, drivers-16 gate extensions | `tests/unit/t_api_abi.py`, `t_mirror.py`, `tools/abilock.py` and `tools/abi.lock.json` exist. Only their reach is new (no `--kernel` mode, no `kerndos/`, no `os88.h` in the fingerprint) |
| C17 | drivers-1/2 driver lifetimes by count | The global `[drv_wcnt]` test (`kernel/memory.inc:2143`) |
| C18 | shell-3 desk-item remove by record (`AH=0FEh`) | `AH=0xFF`, remove by name for the Sticky row (`os88api.inc:1452-1454`) |
| C19 | gkm-3 `tests/smallport.txt` | The port-list precedents `tests/megaport.txt` and `tests/box86port.txt`, and `_ports` in suite.py |
| C20 | gkm-6 retire kern_emu as VMMOUSE.DRV | The DRVC_POINT driver class and USBMOUSE.DRV, which it names |
| C21 | rt-3 the PS/2 runtime as an overlay | XMEM.DRV's `DRVC_OVL` shape, which it names |
| C22 | P5 one boot handoff record | 0060:0004 `boot_cylrun` and 0060:000C `boot_ticks` are already fixed-address handoff words (`kernel/kernel.asm:2346`, `2373`). boot.md cites them |
| C23 | sched-16's measurement leg, a `uilat --workers N` | `tests/uilat.py`; only the argument is new |
| C24 | engsys-15 refuse a ~90%-of-tier selection | The unscoped refusal in `os88test.py`/`os88soak.py`. The review notes that `-k '*'` passes it |
| C25 | files RT2 the aimed-destination pin | The two-caller pin shape exists twice: `mem_in_xfer`'s `[mem_pinseg]`/`[mem_pinimg]` (mover and dropper, `memory.inc:2017`, `3055`) and `mem_fatw_dirty` (shed and dropper, `:2527-2530`) |

---

## 5. NEW: nothing in the tree does these

These were checked and found absent. Each line names what was looked for.

**Gates and tools (0 shipped bytes)**
- `t_ifwin.py` with `tests/ifwin.txt`, and `t_divsites.py` with
  `tests/divsites.txt`: neither exists.
- `tests/stisites.txt`.
- The `IFLAT=1` and `APICHK=1` knobs: neither is in the Makefile's `#>` knob
  list.
- `tools/os88live.py` and `tools/os88contract.py`: neither exists.
  `tools/stkdepth.py` does.
- `@ctx` tags and `t_apictx`.
- A check of SPEC.md's fenced `NAME equ N` lines against the source.
- checkdocs:
  - reading `.c`/`.h`/`.txt`/`.sh` (today it reads `md inc asm py`
    and the Makefile, `tools/checkdocs.py:230-232`);
  - resolving a basename in the citing file's own directory;
  - a warn mode for `docs/reports/` and `docs/history/`.
- AGENTS.md generated from CLAUDE.md.
- A generated routine reference.
- `kernsize` per-class resident lines.
- The build-name ratchet.
- An `owner=` field on suite rows (`Row` has none, `tests/suite.py:141`).
- The `xferfreeze`, `ifoff` and `stkdie` rows.

**Kernel mechanisms**
- The IMR shadow.
- `OSAPI_IRQ` (`drivers/os88drv.inc` has no IRQ door).
- DOS.O88's int 15h filter (xm-1).
- DOS's own int 0, "Divide overflow". DOS only banks vector 00h,
  `apps/dos/dosw.inc:89`.
- The free-cluster count cache. `dsk_free_clus_x` counts the whole FAT on
  every call, `kernel/disk.inc:6329-6352`.
- `OSAPI_FILE_GRAIN`. Today the cluster size comes only from
  `OSAPI_FILE_DFREE`/`OSAPI_VOL_STAT`, both counting, `os88api.inc:1110-1137`.
- The `READ_SEQ` cursor's name stamp.
- The `READ_AT`-shaped stream cell.
- `W_IDX` in the window record (`kernel/wm.inc:128`).
- `wm_isrec` refusals at the package doors.
- The one-shot UI boost.
- The T5 rare-cell door, and the `OSCALL` macro.
- R14's window class.
- The declarative Control Panel engine. Neither `ctrl.inc` nor
  `apps/os88ui.inc` has a page table.
- `kernel/modhop.inc`.
- A shared A20 include. Each driver has its own body.
- An explicit kind byte per LDT entry (gms-R1).
- FILECP/FDLG on kern_big as cached modules.
- KBMOUSE as an init-lifetime module.

**SDK and package features**
- `MENU_SEP`/`MENU_CHK`: only `MENU_DIS` exists, `os88api.inc:4113`.
- Kernel-synthesised bold: there is no bold in `font.inc`.
- `W_ONMENU`: the `W_ON*` words stop at `W_ONTIMER`, `os88api.inc:4162-4184`.
- One double-click interval. Four kernel constants, `ui.inc:66`, and so on.
- The clipboard generation count.
- A format byte on the clipboard (R15). It also reopens SPEC.md 55.1, "Text
  only, and why that is a decision", and `kernel/clip.inc:7-12`. flagday.md
  says so.
- The Open form's extension filter (R16). `OSAPI_FILE_DLG` takes only
  AL/BX/DI/SI, `os88api.inc:1167-1170`.
- `OSAPI_GET_TICKS32`. `OSAPI_GET_TICKS` answers AX only, `os88api.inc:351`.
- `apps/os88lmem.inc`. The only sub-allocator is inside WIN16.
- Parts `OP_INITDROP`.
- The region-movable header bit (mem-17).
- The SPEC 18.4 error-class table: SPEC.md has no CLASS/ACTION/LOCUS
  table.
- The SPEC 7 lock-order numbering (section 2, E5).

---

## 6. What to change in the review

1. **Report section 5, *What we would NOT copy*.**
   - Reword the `.LNK` bullet (E1).
   - Reword the shared-library bullet (E6).
   - Add a sentence to the list's preamble: *each entry refuses a MODEL; where
     os8088 already has the feature in another shape, the entry names it.*
2. **Report section 5, theme 13.** Drop the `pit_now` bullet (E2), or narrow
   it to SPEC 72.15.1.
3. **Report section 5, theme 1.** Replace "`sch_lock` as a nestable Forbid
   ..." with "number SPEC 7's existing `sch_lock` rule and write the lock
   order" (E5).
4. **phase1.md WP26.** Delete the `ui-8` and `isr-3` rows (E3, E4). Narrow
   `gi-7` to 72.15.1 (E2).
5. **phase1.md WP15.** Delete `gi-7` (E2).
6. **phase1.md WP3.** Correct "no `kbm*` row exists" and name
   `wirefleetnomouse` (M1).
7. **phase1.md WP6.** Add `resident` to Rows (M2).
8. **phase1.md WP9 and flagday A4.** Say that the level computation is The
   Wire's existing code, and that the kernel half covers only hand-copied
   packages (M5).
9. **systemic.md theme 2.** The release-time IVT check walks `mem_rr_ivt`
   (M3). The in-service rule reuses the heartbeat's OCW3 read and sits beside
   `[sch_chbusy]` (M4).

**Why four EXISTS items were the review's own work.** The documentation passes
(`14428309`, `10912c6e`) ran AFTER the area findings and before the synthesis
re-read them. The synthesis took its recommendations from the findings, not
from the edited tree. A finding whose fix was pure text was applied in the doc
pass and then carried forward as a recommendation. Any further
recommendation of the "make SPEC/SDK say X" kind should be checked against the
current tree before it is planned.
