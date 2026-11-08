# Phase 2, the flag day - kernel review of 2026-11-08 against ABI-BREAK-LIST

This is the second-phase synthesis of the kernel review: everything the 25 area
reviews and the gap reviews say that needs an ABI break, or that changes a row
already on `docs/plans/ABI-BREAK-LIST.md`. It is a dated reading of one tree
(branch `the-big-review`, `4b9f1715`), not a maintained document. Nothing in
the repository was edited, and nothing was built or booted.

It has three parts:

1. **The existing rows the review touches.** Each is strengthened, weakened,
   refuted or superseded, with the reason.
2. **New rows**, written in ABI-BREAK-LIST's own row style under its own
   section letters. Part 2 is self-contained so that it can be appended to
   the list as **section 13**.
3. **A recommended flag-day bundle**: which rows go together, in what order,
   and what the bundle saves or costs on each kernel.

## How to read it

**Inputs.**

- All 25 `data/*.json` files: 469 findings. The 24 tagged `flagday` are all
  read here, as is every `now` or `record` finding whose better form needs a
  break or that names a row (`listed_as`). The two refuted findings (isr-5
  and wm-20) are dropped; neither touches the ABI.
- The area reports' Rethinks sections.
- Six gap reports. The one that matters most here is
  `gap-flagday-reconcile-abilist.md` (`gfr-*`), a first pass at this same
  reconciliation. Also used: `gap-fault-containment-error-model.md`
  (`gfce-*`), `gap-context-lock-model.md` (`ctx-*`) and
  `gap-kernel-matrix-near-model.md` (`gkm-*`).

**Verdict tags on every source id:**

| tag | meaning |
|---|---|
| **[c]** | confirmed by the review's verifier |
| **[k]** | corrected by the verifier; the corrected figure is the one quoted |
| **[u]** | unchecked; lower confidence |
| **[g]** | from a gap review; those have no verified data file, so treat as unchecked |
| **[s]** | checked against the tree by this synthesis, by grep or by reading `build/*/kernel.map` |

**Byte conventions.** Every byte figure is ESTIMATED unless it says MEASURED.
Figures are signed (minus is a saving) and say which section pays:

- **resident**: `.text`, `.cold` or `.bss`. `.cold` IS resident, billed by
  `KERN_BUDGET` exactly like `.text`.
- **transient**: `.boot2`, `.ovl` and the boot sector.
- **module**: bytes inside a `.DRV` image, not resident.
- **kern_mega conventional**: kern_mega's `.text` and `.lowbss` only. On that
  kernel `.cold` and `.mega` run above 1MB after the switch, so a `.cold`
  saving there is not a conventional one.

kern_emu carries every kern_big figure, because `KERN_EMU` implies `KERN_BIG`.

**Corrections this synthesis makes to the inputs, found in the tree [s]:**

- **gfr-1 names `OSAPI_GFX_FILL_GRAY` as a rare cell to promote. It is
  already a hot SLOT** (`kernel/kernel.asm:2660`, 0x0048).
- **gfr-2 prices the door on the `cw_` shims at about -140, from 70 `cw_*`
  symbols.** `docs/plans/KERNEL-BYTE-QUEUE.md`'s own 2026-11-08 status line
  counts **45 shim bodies and 23 zero-byte aliases**. An alias has no body to
  lose and would *gain* a table word, so the realistic figure is about -45 to
  -90 (row P4).
- **histsys-7's "no driver change" is wrong.** The verifier found that
  `drv_publish` zero-fills and copies only as far as `DRV_H_DSV`
  (`kernel/driver.inc:4049-4078`). Two shipped drivers declare short tables
  (`drivers/hdd/hdd.asm` and `drivers/ems/ems.asm` pass `OS88_DRIVER`'s
  fourth argument), so a read in place would run into the driver's name
  string. Row D4 is the flag-day fix for that.
- **Neither gfr-6 nor histsys-12 priced the Drivers page.** If the page reads
  titles off the driver files, an XT pays one header read per driver.
- **gfr-5's "+15 bit 5" for the clean bit changes the stack class.**
  `instance.inc:2106-2116` masks the class byte with `0x3F`, so bit 5 makes
  the index out of range, and that gets the largest stack. The result is
  harmless on an old kernel (no refusal, just more stack). A new kernel must
  mask with `0x1F` (row L7).
- **The census here is MEASURED, by a grep of `call OSAPI_*` outside
  comments in `apps/` and `drivers/`.** Rare names were resolved through the
  `; 0xNNNN` comments in `kernel.asm`'s table:
  - **6,954 call sites, 2,736 of them to the 116 SDK names that are rare
    cells on every kernel.**
  - `tests/` adds 656 rare sites and `drivers/` holds 193 of the 2,736.
  - gfr-1's 6,998 / 2,767 count by a looser rule; the two agree to about 1%.

---

## Verdict

The list and the review fit together. The review does not so much add rows as
change three of the list's biggest rows into better designs. It also shows that
several flag-day halves can be taken now, at no bump, which makes the flag day
itself smaller and safer.

1. **The table row is the software-interrupt door (T5), not five-byte cells
   (T1).**
   - Savings: about **-385** resident on kern_big and kern_mega (conventional),
     **-245** on kern_small, **-2 bytes at each of 2,736 rare call sites**,
     and an unknown cell becomes a refusal instead of a wild far call.
   - The price: about **+150 to +215 clocks a rare call**, roughly twice what
     bytes-1 quoted.
   - T1 stays as the fallback.
   - The renumber must sort cells by MEASURED frequency (V7).
2. **The window-handler row is a class table in the package (R14), not one
   indexed setter (K17-3).**
   - K17-2 survives as one runtime flag door.
   - Six of T2-2's ten kern_mega ICW stubs go with it, at no extra cost.
3. **Three flag-day halves move to now, with no bump:**
   - **the API level (A4)**: byte 31 of the header and 0060:0006, both free
     today;
   - **the kern_mega clean bit (L7)**: L3's staged road;
   - **the OSCALL rewrite (T6)**: byte-identical, so it can be proved with
     `cmp`.
4. **L1 is worth its row today.** The pad is MEASURED at **386 / 309 / 232**
   bytes (kern_big / kern_small / kern_mega) in the current maps. After T5 it
   shares the same rebuild at no extra churn.
5. **Two rows are refuted and one is overtaken:**
   - **C3 is refuted by arithmetic.** It saves 6 kernel bytes and costs about
     +300 package bytes across 154 sites.
   - **T4 is refuted harder**, because a rare cell becomes 2 bytes.
   - **F2-1 is already BUILT** (SPEC.md 20.13.9).
   - **C4 is half dead.** `OSAPI_FILE_APPEND_SYS` has zero callers.
6. **The driver cluster changes shape:**
   - histsys-7 (now) plus a service-table floor at DRV_VER 14 (D4) makes D1
     and drivers-9 moot;
   - the IRQ registry (D8) is the driver half of L3 on every kernel.
7. **The bundle saves roughly -1,100 resident on kern_big, -690 on
   kern_small and -960 conventional on kern_mega, all ESTIMATED.** About a
   third of each is L1's pad, which is MEASURED today but varies 0-511 by
   build. Packages lose about **-5.5KB unpacked** at the rare sites, and about
   **-1.5KB** more where class tables replace setter code. The bump is
   `PKG_FMT` 10 -> 13 and `DRV_VER` 12 -> 14 (list section 0, item 2).

---

## 1. Existing rows the review touches

Verdicts:

- **S**: strengthened (new evidence);
- **W**: weakened;
- **X**: refuted (an argument);
- **SUP**: superseded (taken, or moot - the tree was checked);
- **R**: refreshed (the row's facts restated from the code).

Rows not listed are untouched: T2, C5, C6, C8, R3, R4, R5, R7, R8, D2, F1, L2,
L4, L5, P1, A1-A3, V4-V6, R2-2, R2-3, R2-4, R2-5 and P2-1. Each was checked
for still applying where it is in the bundle.

| row | verdict | reason | sources |
|---|---|---|---|
| **T1** | **SUP by T5**, kept as the fallback | The door at the site (3 bytes against 5) and a 2-byte table word buys about 3x T1's ~-117. T1 survives only if T5's DOS-vector or IF proof fails. abi-4's literal `[bp+10]` frame read in `dwsf_dskw_write_seq` is a hidden reader of the frame that either row changes; take `API_RN_DSOFF` now | bytes-1 [k], gfr-1 [g], abi-4 [k] |
| **K17-1** | **merge into T1** (the same row), then **SUP by T5** | Same five-byte design, priced twice, "refused once" | gfr-10 [g] |
| **T3** | **refused, unchanged; part recovered inside rule 4** | Under T5, seven of the thirteen small-kernel refusal cells are rare cells: `wm_band`, `xm_alloc`, `xm_free`, `vol_stat`, `drv_cfg_x`, `drv_dlg_x` and `pkg_rehome_x`. Each becomes a 2-byte word pointing at one shared refusal, about **-28 of the ~78**, with the index still present and refusing (SPEC.md 20.8 rule 4 intact). The six hot ones stay as today | gfr-1 [g] |
| **T4** | **X harder** | A rare cell becomes 2 bytes under T5; no multiplexed verb is cheaper | gfr-1 [g] |
| **T2-2** | **SUP in part, X for the rest** | Of the ten `MEGA_ICW` cells, six are handler setters (`W_ONSIZE`, `W_ONSZ`, `W_ONWK`, `W_ONRC`, `W_ONCLICK`, `W_ONWH`), and R14 deletes them. The four left (`gfx_color` = `SET_COLOR`, `osapi_seed`, `gfx_b1ink`, `ico_c1`) are kern_big's 52-cycle inline cells, worth about -32 on kern_mega. **Refuse the remainder**: XT cycles beat 32 conventional bytes | gfr-3 [g] |
| **K17-2** | **R**: survives, reduced | It becomes one runtime `OSAPI_WM_FLAG` (AL set/clear, AH bit) beside R14. Creation-time flags become the class's style word. MEASURED asm census [s]: 107 flag-setter call sites (SNAP 30, OWNBG 27, SAVEU 19, KEEPH 14, SIZABLE 12, NOANIM 4, QUIET 1) | gfr-3 [g], abi-11 [k] |
| **K17-3** | **SUP by R14** | Folding ten setters into one indexed cell keeps 13 words a window in `.bss`. R14 removes the words and the setters together, so taking K17-3 first would spend a renumber on a cell R14 deletes. MEASURED [s]: 199 handler-setter call sites (ONWAKE 31, ONCLOSE 31, ONTIMER 29, ONMOUSEUP 28, ONDRAG 24, ONRESIZE 20, ONWHEEL 18, ONCLICK 10, ONRCLICK 7, ONSIZE 1), five of them in drivers | abi-11 [k], histsys-9 [k], bytes-6 [c], gfr-3 [g] |
| **K17-4** | **S in bytes, S in price** | Under T5 each demoted SLOT saves 6, not 2: **-78 rather than -26**. But a rare call then costs about +150 to +215 clocks over a SLOT, not ~+78, so the per-cell frequency reading the row already asked for binds harder. It becomes part of V7 | bytes-1 [k] (verifier: "makes K17-4 more valuable, not less"), gfr-1 [g] |
| **K17-5** and **C7** | **one row, recorded** | The same cell: `OSAPI_DRV_CALL_AT` is `drv_pkg_call_x`'s SLOT at 0x038B (`kernel.asm:3715`), next to the XCELL at 0x0348 (`:3558`). The list gives it two statuses. Two call sites, both in FTPD [s]. Keep C7's "recorded, not proposed" and delete K17-5 | gfr-10 [g], [s] |
| **C1** | **S on correctness; bytes about halved under T5** | The KB-to-paragraph shift has now carried the bit-10 carry defect at four doors, each found separately: claim AX, regrow AX, the DMA head CX and `mem_claim_x`'s overflow test. The paragraph door has no shift. Under T5 the six withdrawn cells save 12, not 36, so the row is about **-35 to -55 per kernel**; on kern_mega only the cell part is conventional. MEASURED sites [s]: 217 (MEM_CLAIM 127, AVAIL 46, DMA 16, DMA_HI 14, REGROW 11, CLAIM_HI 3) | mem-18 [u], mem-5 [c], hm-6 [k], md-16 [c], gfr-7 [g] |
| **C2** | **unchanged; smaller under T5** | -15 becomes about -11. 11 call sites [s] | gfr-7 [g] |
| **C3** | **X, by arithmetic** | It saves 6 kernel bytes (4 under T5), but every one of **154 call sites** (GOTO 8, GOTO_Q 20, GOTO_QM 126; MEASURED [s]) then needs a `mov al, mode`: **about +300 across packages**. A cleanliness argument alone does not carry a flag-day row. **Status: proposed -> refused** | gfr-7 [g], [s] |
| **C4** | **R: half dead; take the dead half** | `OSAPI_FILE_APPEND_SYS` has **zero callers** in `apps/`, `drivers/`, `tests/` and the C SDK [s]. HDD.DRV's installer writes with `OSAPI_FILE_WRITE_SEQ` + `WSEQF_SYS` (`drivers/hdd/inst.inc:2246-2250`). Withdraw APPEND_SYS alone and leave WRITE_SYS (2 sites) alone, which deletes the "every caller zeroes AL" cost. **-17 kern_big, MEASURED spans** (`build/kernel.map`: the JCELL 3 bytes, `api_file_append_sys` 9 at 0x5D0, `dwf_dskw_append_sys` 5 at 0xAD10). About -3 kern_small. **-12 conventional on kern_mega**, where the thunk at 0x179F4 is `.cold`, above 1MB. The row's "one caller" and the installer's comments (`inst.inc:127-129, 984-986, 2052-2053`) are stale | gfr-7 [g], [s] |
| **R1** | **S** | Re-checked: both forms and `ld_rehsz` are still there (`loader.inc:1639, 1776, 2167, 2262`), with one call site, the KEEP form [s]. hm-11's proposed `OP_INITDROP` would be a second KEEP-form caller, and the bytes form would still have none. Bytes on kern_mega: the `.cold` part (`ld_start`) is above 1MB, so about -6 conventional, not -16 | hm-11 [k], [s] |
| **R2** | **unchanged, keep** | Correctness. Nothing in the review weakens it | - |
| **R2-6** (handles) | **W** | gfr-4 corrects the case for memory handles: (a) kern_mega's measured forks (+48, +97, +42, +88, +47) are `.cold`, above 1MB, not conventional; (b) packages would pay +2 to +4 at each of 367 FREE/MOVABLE sites (MEASURED [s]: FREE 246, MOVABLE 118), sites C1 does not convert, so "one conversion with C1" is wrong. hm-2's now-fix (+6 to +8) buys the correctness half alone. **Stays recorded; hm-10 folds in** | hm-10 [c], gfr-4 [g], hm-2 [k] |
| **R6** | **unchanged (shelved); two riders** | wm-R3 (window handles) is refused while R6 is shelved and re-raised only with it: the record is readable by contract, so a handle breaks every package that reads `W_X`. histsys-17 (now, -4 to -8) groups the eight implicit display-context bytes as `gfx_dc`, which gives R6 a home | wm-R3 [u], gfr-4 [g], histsys-17 [c] |
| **N1** | **S, re-checked; take now** | Both fallbacks are still present (`driver.inc:2979-2981` for FSV and `:4071-4072` for DSV) [s]. The cost is 7 + 4 bytes and the fix fails closed. D4 then removes the rest of the length handling at the bump | gfr-6 [g], [s] |
| **D1** | **SUP if histsys-7 lands** | histsys-7 (now) keeps class 1's copy and reads the others in place, after which `drv_svcix` has nothing to index. The verifier's correction means histsys-7 needs either a per-read length test (+50 to +110) or a table floor at DRV_VER, and that floor is new row D4. drivers-9's common-prefix reorder (-98 `.bss`) has nothing left to shrink. If histsys-7 is rejected, D1 + drivers-9 remain one row (about -6 to -10, and -98 `.bss`) | histsys-7 [k], drivers-9 [c], gfr-6 [g] |
| **D3** | **S** | The +16 name field already carries the driver's title (`OS88_DRIVER 'Sound', ...` against `DRV_TITLES`' `'Sound'`), so the resident titles duplicate the files. That half feeds D7 | histsys-12 [u], gfr-6 [g] |
| **F2** | **unchanged (recorded)** | gfr calls F2 superseded by bytes-8. bytes-8 drops the 4-byte `.DRV` extension from the module-name strings (no ABI, about -35 / -15); F2's -2 is the `WIN95` -> `WIN` stem and a user-visible file rename. They are neighbours, not one row | bytes-8 [k] |
| **F2-1** | **SUP: BUILT** | SPEC.md 20.13.9 ("A stream may be FILTERED - the near-call escape", `LZ_ESC`, `kernel/lz.inc:90, 252`), with no container change and no bump [s] | gfr-10 [g], [s] |
| **L1** | **S: MEASURED today** | `.text`+`.bss` against the 512 rounding: kern_big 0xB27E -> 0xB400, **386**; kern_small 0x80CB -> 0x8200, **309**; kern_mega 0xC918 -> 0xCA00, **232** [s, the three maps]. The row's "165 at build 477" is one dated reading; the pad is 0-511, about 256 on average, and a one-off rather than a rate. `FAT_SEG equ KERNEL_SEG + KIMG_PARA` is unchanged (`kernel.asm:1804`). After T5 a rare site names no segment, so L1's churn falls to the hot SLOTs, ES-on-callback and the tools, and both rows bump both numbers: **one flag day, not two** | gfr-8 [g], [s] |
| **L3** | **S: a staged road that does not wait for The Wire** | (a) the PM-clean bit (L7, now, +12 `.mega`) lets a converted package say so, after which `mega_gpw` closes it rather than completing the store; (b) the kernel-owned IRQ cell (D8) is the `mega_null0`/`mega_db` half; (c) mc-10's `[mega_kfix]` counter (+8 `.mega`) proves the kernel itself clean; (d) gkm-9 places L3 as the precondition of a one-disk 8088/286 product, which bears on the owner's open question of whether L3 is a size row or a product row. Still out of the bundle until The Wire serves kern_mega builds (the owner's ranking) | mc-19 [u], mc-10 [c], isr-13 [c], drivers-6 [c], gkm-9 [g], megacore Rethink 4 |
| **P2** | **S** | drivers-16: `VX_ABI_VER`, `XM_ABI_VER` and `HD_ABI_VER` are versioned by hand, and `tools/abi.lock.json` fingerprints none of them. Add them to `abilock.py` (0 shipped bytes), so that "moves freely" stays true only when the version moves too | drivers-16 [c] |
| **V1** | **S / R** | (a) It must be a **new package-facing entry** beside the raw body, because kernel callers depend on the preserve-everything body: `gfx_hline` returns BX from the CX `gfx_fill` kept (`vga12.inc:1070-1079`), and `gfx_xor_strips` carries BP across four calls. Cost +~6. (b) The C SDK already treats every register as volatile (`os88thunk.asm:58-66`), so the C half is zero-risk; take it first. (c) The asm half is gated on a lint | gfx-16 [c], abi-17 [c], gfr-9 [g] |
| **V2** | **S** | Its renumber is free inside T5's | gfr-1 [g] |
| **V3** | **unchanged (recorded)** | Under T5 the rare penalty rises to about 255 clocks over a SLOT, but the trio has **7 call sites in the whole tree** (RUN 2, WALK 2, SEL 3; MEASURED [s]). Promotion still costs +6, so the decision does not change | gfr-1 [g], [s] |
| **R2-1** | **unchanged; restated** | Its -223 is `.mega` (above 1MB), not conventional. It rides the bundle as kern_mega-only | gfr-10 [g] |
| **section 11** | **SUP in part** | Bullet 1 (SND_PLAY's blocking text in SPEC.md 20.3) and bullet 3 (the dead 8-byte cold-cell macros) no longer grep in the tree [s]. abi-10 and engsys-5 replace them with a wider point: the SDK and SPEC.md 20.3 still keep two contract copies that disagree in both directions, and the cure is to delete the SPEC copy, not to edit it | abi-10 [c], engsys-5 [c], [s] |

**Refused rows respected.** Only K17-1 is re-proposed, and only in a new form
(T5) with three new arguments: about 3x the bytes; a run-time refusal of unknown
cells, which closes loader-6's hole; and a rehearsal on kernel-private traffic
first (P4). T3 stays refused; T5 recovers part of its bytes without breaking
rule 4. ui.md's "per-event handler words beat one window procedure - recorded,
not recommended" refuses a Win16 `WndProc`. It does not refuse R14, which keeps
per-event vectors.

**List hygiene to apply with section 13:**

- mark F2-1 BUILT;
- rewrite C4 as APPEND_SYS alone;
- merge K17-1 into T1;
- merge K17-5 into C7;
- mark C3 refused;
- mark T2-2 superseded in part;
- mark section 11 bullets 1 and 3 done.

---

## 2. New rows (to append to ABI-BREAK-LIST.md as section 13)

## 13. Rows from the kernel review of 2026-11-08

From the 25 area reviews and the gap reviews of
`docs/reports/big-review-2026-11-08/`. The reconciliation is
`synth/flagday.md`, which also carries the verdicts on rows 1-12.

**Source tags:**

- **[c]** confirmed by the review's verifier;
- **[k]** corrected by the verifier (the figure quoted is the corrected one);
- **[u]** unchecked;
- **[g]** from a gap review, with no verified data;
- **[s]** checked against the tree by the synthesis.

Every byte figure is ESTIMATED unless it says MEASURED, and the figures are
signed by section. **"conv"** means kern_mega conventional: its `.text` and
`.lowbss`.

The **bump** column says what the row needs on its own: `PKG_FMT`, `DRV_VER`,
both, or **none**. **none** means an `abilock.py --update` without a bump, or a
private version.

The ids continue the list's own sequences. The gap review's provisional ids are
kept where they did not collide; its A4 (the clipboard format) and A5 (the Open
filter) are **R15** and **R16** here, because each re-contracts a register
callers leave unset today, so neither is an addition.

### 13.T The API table's shape

| id | what changes | who it breaks | what it buys | bump | source | status |
|---|---|---|---|---|---|---|
| **T5** | **The rare cells behind one software-interrupt door.** A rare call becomes `int OS88_RARE / db index` at the site (3 bytes against 5), and the kernel keeps a 2-byte word per rare cell (116 SDK names; 112 rare cells in kern_big's decoded table). The door: <br>- fetches the index through the caller's CS:IP and bumps the return IP; <br>- picks the kind by index RANGE (the renumber sorts the cells by kind, so no kind byte is stored); <br>- loads BP and runs today's `api_rs`/`api_rsc`/`api_rx`/`api_rxc`/`api_rn` bodies unchanged; <br>- returns with `retf 2`, so CF/ZF answers survive; <br>- re-enables interrupts only if the caller's stacked IF was 1. <br>An index past this kernel's end answers CF=1, AX=1 (R9). Hot SLOTs and the X, inline, jump and far cells are untouched. <br>**Vector:** a number above 3Fh, so windowed DOS's exchange set (00h, 08h-0Fh, 10h-3Fh; `apps/dos/dosw.inc:75-80`) never banks it. Full-screen DOS's whole-IVT bank (`apps/dos/dos.asm:2956-2961`) and a DOS program hooking the vector must still be designed out. <br>**kern_mega:** an IDT gate at the same privilege, with the index fetched through the readable code selector | Every package, driver and C package. Every rare site re-assembles and the rare set renumbers. Also affected: <br>- the C SDK's thunks (65 rare sites in `apps/cc`); <br>- `t_api_abi`'s decoder; <br>- `tools/os88wire.py` and `os88pkg.py`, whose highest-cell scan reads far-call immediates (`os88wire.py:120-140`); <br>- every test that hard-codes a cell; <br>- `kern_dos` and `DOS.O88` (vector ownership); <br>- abi-4's frame reader (FLAGS now sits under CS:IP) | **Kernel:** about **-385 `.text` kern_big and kern_mega (conv)** and about **-245 kern_small**, from -672 of cells, +224 of table, +~45 door, +~6 for the `retf 2` tails and +~12 for two promotions (V7). <br>**Packages:** **-2 bytes per rare site: 2,736 sites in `apps/` + `drivers/` (MEASURED [s]), about -5.5KB unpacked** (less once packed, because a far call's absolute operand already repeats under LZ). The largest: WIN16 -378, RealDMG -266, Pixel -246, DOS -224, XTMP3 -194, Video -180, Browser -166, Arcade -156, HDD.DRV -146, Scribe -144. <br>**Correctness:** a newer-SDK cell on an older kernel is a refusal, not a wild far call (loader-6's run-time half). <br>**Cost:** about **+150 to +215 clocks a rare call, 30-45 us, PREDICTED**. bytes-1's +80-100 left the middle of the door unpriced | both | bytes-1 [k], gfr-1 [g], abi-4 [k], loader-6 [k]; replaces T1/K17-1 | **proposed - the table row.** It re-proposes K17-1's declined renumber with three new arguments (bytes, run-time refusal, rehearsal first in P4). **T1 is the fallback** if the DOS-vector or IF proof fails. Open: are any rare cells legitimately called with IF=0? abi-8's tags answer that. "Rare cells: IF=1 callers only" would drop about 30 clocks |
| **T6** | **One call macro at every site, and the names normalised.** Every `call OSAPI_X` (6,954 sites [s]) becomes `OSCALL OSAPI_X`, which emits the far call or, after T5, the door, according to the cell's kind. The rewrite is mechanical and **byte-identical** until T5 flips the macro, so it is proved with `cmp` over every image. Renames ride the same pass with one cycle of `%define` aliases: CLIP = clipboard against `WM_CLIP` = clip region, FS = file listing against `FS_PROG`/`FS_OP`, DESK's three meanings, and the outliers of the SUBSYS_VERB shape (GET_TICKS, SET_COLOR, FULLSCREEN, SEG, ...). Verbs move to AL (BL when AL is an argument) only where another row reopens the cell | No binary. The source of every package. Out-of-tree source gets the aliases | 0 bytes. A cell's KIND stops being source ABI, so promoting or demoting a cell is a rebuild rather than an edit at every site. It is also the only churn-free moment for renames | none (SDK text) | gfr-1 [g] (cost 1), abi-13 [u], abi-12 [c] | **proposed - the rewrite NOW (byte-identical), the renames at the flag day** |

### 13.C Duplicate doors and consolidations

| id | what changes | who it breaks | what it buys | bump | source | status |
|---|---|---|---|---|---|---|
| **C9** | **One file-position shape.** `OSAPI_FILE_WRITE_SEQ` takes `READ_SEQ`'s 16-byte caller cursor and the same buffer register pair. Its token becomes a field of the cursor, and the HOLD stays the kernel's, one at a time. Today the read stream is name + cursor at ES:DI with the buffer at DX:BX, and the write stream is name + token in DI with the buffer at ES:BX | 23 WRITE_SEQ call sites in 12 files [s]: DOS, Browser, Pixel, FTPD, XTMP3, Telnet, WaveRack, `os88wseq.inc`, and the drivers RAMDISK.DRV and HDD.DRV (the installer). Also the C SDK | 0 to -30 kern_big. It ends `dws_rec`'s thrash between two concurrent streams: each cold token commits and falls back to APPEND, a name lookup plus a whole-chain walk, O(n^2) over the file | both | histsys-13 [u], dw-22 [c] | **recorded.** It wants a census of concurrent writers first. dw-22 names two FTPD sessions, or an upload during a DOS COPY; whether FTPD serves two sessions was not checked |
| **C10** | **`OSAPI_DESK_ITEM` remove by record only.** The zone argument (AH) is retired, and remove names the row by its record (volume + path), as Phase 1's additive `AH = 0FEh` verb already does | The DESK_ITEM callers: `apps/win16/w16progm.inc`, `apps/os88stik.inc` and `drivers/wirezone.inc` (6 sites [s]) | **-6 `.cold` kern_big** (`desk.inc:2283-2284`). WIN16 -12: one call in place of a 16-probe loop that loads a module per probe | both (a driver calls it) | shell-3 [c] | **proposed, low - fold-in only** |

### 13.R Register contracts and records

| id | what changes | who it breaks | what it buys | bump | source | status |
|---|---|---|---|---|---|---|
| **R9** | **One refusal shape.** CF=1 stays the only universal sign. AX=1 means "no such service or verb on this kernel" in every cell, and family codes start at 2. What moves: <br>- `OSAPI_DRV_CALL`'s no-driver AX=0 (`driver.inc` `.none`, about 6655-6662); <br>- every bare `stc` stub that leaves AX untouched (`snd.inc:710-713`, which serves three cells, and kern_mega's real-mode bracket, which fills the table with `stc`, `fsx.inc:2341-2351`). <br>Most move by sharing `OSAPI_XREFUSE`, which already answers 1, as do `FERR_NODISK` and `FSKE_NOTHERE`. Explicitly **not** one renumbered error space: no consumer mixes families. The DOS-3-style CLASS/ACTION table goes into SPEC.md 18.4 now, at 0 bytes | Any package that reads DRV_CALL's AX=0 as "no driver". It needs a census; the SDK tells callers to test CF | About 0 to +10 resident (+3 for a stub that keeps its own body, 0 where it shares XREFUSE). One answer a package can test for "not on this kernel", which T5's past-the-end index also returns | PKG_FMT only if the census finds an AX=0 reader; otherwise none. Ride the flag day either way | abi-9 [c], gfce-8 [g], histsys-14 [u] | **proposed** (the doc half now) |
| **R10** | **Deferred damage, opt-in.** A per-window (under R14, per-class style bit) "paint later". Damage goes into a global four-rect list (32 `.bss`) through `wm_clip_tab`'s splitter, drained at the top of the UI pass. The default stays synchronous, which is OS/2 PM's `CS_SYNCPAINT` shape inverted. Only a GLOBAL deferral changes `OSAPI_WM_SHOW`'s "the paint has run" contract | Opt-in: nobody. Global form: every package that shows a window and then draws in it | About +80 to +100 resident (ctx-9's reconciliation of histsys-10's +60..+120 and wm-R2's +60..+100). Coalescing, and a shallower nest on the UI task's 1,024 bytes. The safety half (teardown on the UI task) is ctx-1/histsys-1, now, with no ABI | none (opt-in) / PKG_FMT (global) | histsys-10 [c], ctx-9 [g], wm-R2, histsys-1 [c] | **recorded - if built, build it opt-in, needing no bump** |
| **R11** | **32-bit ticks.** A high word bumped on the carry. `OSAPI_GET_TICKS` answering DX would re-contract a 329-site cell whose callers may keep DX across it [s], so the better form is an appended `OSAPI_GET_TICKS32` | Appended: nobody | +2 `.bss`, +~6 `.text` and a cell. It lifts the 30-minute deadline bound and the saver's 59-minute cap | none | sched-19 [c], sysmisc-11 [u] | **recorded until a package asks** |
| **R12** | **One display-index register.** CH = display (0 = the caller's window's) on `OSAPI_FSX_CAPS`, `_RUN`, `_SURF` and `OSAPI_VIDEO`. FSX_CAPS's overload of BX = 1..15 as a display index is retired | 39 FSX_CAPS + 32 FSX_RUN + 10 FSX_SURF call sites [s]; `EXTF_CAPS` | -6 resident kern_big (fsx_caps' range arms) and a simpler EXTF_CAPS | PKG_FMT | video-19 [c] | **proposed, low - fold-in** |
| **R13** | **A claim's owner word is the instance slot** for everything a package holds (drivers keep segments). Today `MC_OWN` means a slot, a segment, a cache tag or a kernel tag, and the region/data split costs code in `mem_find_own`, `mem_rr_tab`, `mem_reloc_call`, `mem_free_rec_x` and `MEM_OWNER_MAX` | The Task Manager, which reads `CLS_OWN` in `OSAPI_CLAIM_SNAPSHOT` | -20 to -40 `.cold`, unmeasured (not conv on kern_mega) | PKG_FMT | mem-13 [c] | **recorded - ride C1 only if the Task Manager is touched anyway** |
| **R14** | **The window class lives in the package.** The record's handler words (13 on kern_big, 10 on kern_small) become one `W_CLASS` word: a near pointer into the package image to {length byte, style word, handler offsets in a fixed order}. `wm_pkgcall` reads `[W_SEG:W_CLASS+n]` with DS already the package's. <br>- A slot past the length byte is "no handler", so a package still pays nothing for an event it never asked for, which keeps the per-event vector ui.md defends. <br>- The creation-time flags become the style word's defaults. Runtime changes go through K17-2's one `OSAPI_WM_FLAG` (AL set/clear, AH bit). <br>- A runtime handler change (`os88stik.inc:987-1041`, `deskclok/dkset.inc:230`) points `W_CLASS` at a table of its own. <br>- Kernel windows share const tables in `.cold`. <br>- kern_small never reads the ONDRAG, ONTIMER or ONWH slots, so its CF=1 setter cells vanish. <br>- The 28 kernel dispatch reads go through one helper. <br>- No package reads a handler word through the record (bytes-6's grep) | Every windowed package: the fixed 8-word `WT_*` template, **199 handler-setter and 107 flag-setter asm call sites** (MEASURED [s]), five of them in drivers (`saver`, `thmpal`, `hdd` x2, `ether`). Also `os88stik.inc`, `deskclok`, and the C SDK (setter prototypes and thunks) | **Kernel, under T5:** about **-215 kern_big** (-216 to -264 `.bss` of record words, about -32 of withdrawn cells, +~40 dispatch, +~40 of kernel class tables, K17-2's one door replacing seven bodies of 129 MEASURED bytes); about **-95 kern_small** (-108 `.bss`); about **-270 kern_mega (conv)**, which includes six of T2-2's ten `MEGA_ICW` stubs at 11 bytes each, with its class tables in `.cold`, above 1MB. Without T5: abi-11's -200 to -260 kern_big. <br>**Packages:** about **-1.5KB unpacked** net (+~1.3KB of tables replacing ~2.6KB of setter code). <br>**Cost:** +~20 clocks a callback. <br>**Every later callback** (ui-19's `W_ONMENU`, +24 `.bss` today) costs a class slot and no `.bss` | both | abi-11 [k], histsys-9 [k], bytes-6 [c], gfr-3 [g]; supersedes K17-3 | **proposed - replaces K17-3; K17-2 survives inside it as the runtime flag door** |
| **R15** | **A format byte on the clipboard.** AL = format (0 = TEXT) on `OSAPI_CLIP_PUT`, `_GET` and `_SIZE`; anything else answers CF=1 until the second format SPEC.md 55.1 names (Paint's bitmap) arrives as data, not as three new cells | 39 call sites (PUT 16, GET 14, SIZE 9 [s]) must zero AL: up to +78 across packages where AL is not already 0. Also the C SDK | +8 `.text` resident. The second format arrives without three new cells | PKG_FMT | ui-13 [u] | **proposed, low - only inside a flag day** (gap review's "A4") |
| **R16** | **An extension filter on the Open form.** AH = a filter (0 = none; otherwise an index into the app's declared extensions, or a pointer to one 3-byte extension). Non-matching rows are greyed (SPEC.md 47), not hidden, and refused on commit. `fdlg_open_x` overwrites AH today and the contract defines none | The Open callers of `OSAPI_FILE_DLG` (36 FILE_DLG sites, Save included [s]) must zero AH. Also the C SDK | +~30 `.cold` kern_big; FDLG.DRV (a module) on kern_small; not conv on kern_mega. Every app's after-the-pick refusal (SPEC.md 38.6) becomes a greyed row | PKG_FMT | files-18 [c] | **proposed, low, optional - a feature, not a size row** (gap review's "A5") |

### 13.D Driver header, classes and the driver tables

| id | what changes | who it breaks | what it buys | bump | source | status |
|---|---|---|---|---|---|---|
| **D4** | **A service-table FLOOR, not only a length.** At DRV_VER 14 every driver's DSV table is at least `DSV_SIZE` (as it stands at 14) and its FSV table at least `FSV_SIZE`. `DRV_H_DSV` and `DRV_H_FSV` stay as lengths, but only for cells added after 14, so growing a table later still needs no bump. Below the floor: <br>- the `0 = 28` fallbacks (N1) go, if not already taken; <br>- `drv_publish`'s zero-fill and clamp go (`driver.inc:4049-4078`); <br>- histsys-7's in-place reads of the non-sound tables need **no per-read length test**. Without the floor they would read past a short table into the driver's name string. That is the verifier's correction, which cut histsys-7 from -150 to -90..-140 | HDD.DRV and EMS.DRV, the two drivers that declare a short table (`OS88_DRIVER`'s fourth argument: `hd_svc_end - hd_services`, `em_svc_end - em_svc` [s]). They pad to the floor on their own disks. Every driver is rebuilt | About -15 to -25 resident kern_big (the clamp and zero loop), plus most of histsys-7's +50..+110 of length tests recovered. **histsys-7 + D4 together: about -150 kern_big** (this synthesis, ESTIMATED). kern_small 0 (nothing is published there). kern_mega conv: only the part in `.text` | DRV_VER | histsys-7 [k] (verify note), drivers-9 [c], N1 | **proposed.** It makes D1 and drivers-9 moot |
| **D5** | **Typed driver slots instead of one slot per class.** A table of N attached-driver slots, each tagged with a type. Volumes record a slot, and a package asks for the first slot of a type. File redirection has used four class numbers (5, 8, 9, 10), each an ABI constant plus a 36-byte copy | Every driver header, `OSAPI_DRV_CALL`'s AL, and the Drivers page | About 0 to -40 resident (`drv_svcix` goes; arrays sized by slot count). A new file driver needs no new constant. ETHER and PCIETHER could coexist, but a machine has one NIC | DRV_VER (+PKG_FMT for AL) | drivers-15 [c], gfr-6 [g] | **recorded** |
| **D6** | **A request block for `DSV_BLK` and the `FSV_*` verbs.** ES:BX -> {verb, class/unit, status, LBA/count or name/offset, buffer far ptr, completion word = 0}. One dispatcher replaces `drv_blk_call_x`, `drv_fs_call` and kern_mega's `drv_fs_hi`, and the `drv_blkcls`/`drv_blkfp`/`drv_blkseg` staging goes. **Scoped away from DSV sound and IRQ0**: there drivers.md's "Not recommended: a DOS-style request packet" stands, because the direct near-proc table is cheaper on an 8088. Package `OSAPI_FILE_*` registers are unchanged | The transfer paths of five drivers (HDD, RAMDISK, CDROM, HTTPFS, NET's file side) | Kernel about -60 to -120 resident, unmeasured. One status channel. An async door left open, with no consumer | DRV_VER | histsys-8 [c], gfr-6 [g] | **recorded** |
| **D7** | **Driver discovery.** The boot overlay scans `SYSTEM\*.DRV`; class and title come from the header (+16 already carries the title, D3); `SYSTEM.CFG` is keyed by name rather than a row bit; only the `DRVC_MAX` publication slots stay resident | Every driver row, `SYSTEM.CFG`, and the Drivers page (CTRL.DRV) | About -200 to -350 resident, +~120 `.boot2` (transient), ESTIMATED. **Price on the target:** a Drivers page that reads titles off the files reads about 12 headers at about 400 ms each, **about 5 s to open on an XT**, unless the titles are cached in `SYSTEM.CFG` | DRV_VER + SYSTEM.CFG | histsys-12 [u], gfr-6 [g] | **recorded, with that price** |
| **D8** | **The kernel owns the IRQ lines, the IVT entries and the IMR.** <br>- **Phase 1 (append, no bump):** `OSAPI_IRQ` set/unset. It refuses a line already held, keeps an IMR shadow and restores in LIFO-checked order; take the IMR shadow alone first (+~25). <br>- **Flag day:** the cell is MANDATORY (a driver that writes an IRQ vector itself is outside contract). That deletes drivers-6's post-detach IVT scan and the image-move IVT walk, and it is L3's "an IRQ is hooked through a cell" on kern_mega | SOUND.DRV (the SB path), USBMOUSE.DRV, ETHER/PCIETHER and EMS.DRV (ad hoc conflict avoidance, `ems.asm:97-100`) | Phase 1 about +100 resident kern_big (the owner's call; kern_small shadow only, about +25), and SOUND.DRV -60. Flag day: -~25 `.cold` (the scan) on the kernel, and kern_mega's IRQ fixups retire under L3. Fixes four things at once: a vector left behind after detach, a kernel-computed `DRVE_BUSY`, image moves with no IVT scan, and kern_mega's segment-0 hook fixups | none (append) / DRV_VER (mandatory) | isr-13 [c], drivers-6 [c] | **proposed - append now (owner's call), mandatory at the flag day** |

### 13.F Files on disk and names

| id | what changes | who it breaks | what it buys | bump | source | status |
|---|---|---|---|---|---|---|
| **F3** | **The compression hint moves off NTRes's case bits.** +12 is 5Ah/5Bh today, and bits 3 and 4 tell NT, Linux vfat (`shortname=mixed`) and macOS msdosfs "lower-case base and extension", so every packed file on every shipped disk lists in lower case on a modern host. <br>- **Now:** a dual-mark reader in `dskw_czknow` and the listing (+6 `.cold`), and `os88disk.py` writes a mark with bits 3 and 4 clear (40h-47h, A0h-A7h, ...). <br>- **Flag day:** drop 5Ah/5Bh (-6). <br>+13 (create-time skew, values over 199 invalid) and +20 (the OS/2/NT FASTFAT EA handle) are decided against a real NT CHKDSK, or U is learned from the `'CZ'` header at open, leaving a 1-bit flag | No binary. Every shipped disk is rebuilt anyway | +6 then -6 `.cold`, net 0 across the two phases. Interop with every modern host | none | dw-10 [c] | **proposed - the reader now; the old mark dropped at the flag day** |

### 13.L Memory layout, segments and kern_mega

| id | what changes | who it breaks | what it buys | bump | source | status |
|---|---|---|---|---|---|---|
| **L6** | **Interrupts-on compaction with an ISR-reachable flag** (Windows' `GlobalFix`, the Mac's `HLock`). A per-claim flag says "an ISR touches this". Unflagged blocks move with IF on; on kern_mega they also cut at descriptor boundaries under `sch_lock`. Flagged blocks move at IF=0, as driver images do today | Packages whose ISR or FSX rate hook touches a claim (WireBall's queue, the serial lease ring of input-4), plus any driver ring that is not already `MC_DMA` | kern_mega's 64KB IF-off piece ends (~16 ms on the Victory 286, KERN-MEGA-PLAN 14.48.3), and so does `mx_alloc`'s `sch_lock` across a full pack (~0.5 s per 2MB, arithmetic). kern_big's `mem_regrow` path-3 IF-off copy (hm-1) gets the rule it needs. +20 `.mega`, +10 to +16 `.cold` kern_big | **none in the opt-in form** (a new `MEMP_*` verb or flag; today's IF=0 move stays the default). PKG_FMT only if the default flips | mc-5 [k], megacore Rethink 2, hm-1 [k] | **proposed as an ADDITION first**; flip the default at a later flag day, once a census of ISR-touched claims is in |
| **L7** | **L3's staged road: a "protected-mode clean" class bit** (Windows 3.0's NE `PROTMODE`). Bit 5 of header +15: bits 3-5 are free, and an out-of-range class is not a refusal (`instance.inc:2114-2116`). `mega_gpw` *closes* a clean package on a CS store, a bug, rather than completing it. `t_megapkg` ratchets the census, and mc-10's `[mega_kfix]` counter (+8 `.mega`) proves the kernel's own stores. **The kernel must mask the class with `0x1F`**: today's `and bx, 0x3F` (`:2107`) turns bit 5 into an out-of-range index, which an OLD kernel answers with the largest stack (harmless, more stack) | Nothing: old binaries keep the fixups, clean ones forbid them | +12 `.mega`, kern_mega only. It starts L3 without waiting for The Wire to serve two builds | none | mc-19 [u], gfr-5 [g], mc-10 [c], gkm-9 [g], [s] | **proposed - NEEDS NO FLAG DAY** |

### 13.P Package-private and kernel-private ABIs

| id | what changes | who it breaks | what it buys | bump | source | status |
|---|---|---|---|---|---|---|
| **P3** | **The DOS launch block reorganised at the next `KDL_VER` bump.** A header with 16 reserved bytes, the body, and a per-kernel tail named by a flag. Today every recent field was squeezed into a reserve that happened to exist (RSVD, HBPAD, POUT, MRSV), and the tail is a union by build | `kern_dos` and DOS.O88 together (they ship as one) | 0 bytes. The next field costs no hunt | KDL_VER (private) | md-24 [u] | **recorded - when the next field arrives** |
| **P4** | **T5's door, rehearsed on the kernel's own `cw_` shims.** It becomes a sixth door kind ("near body, DS already the kernel's"). A module's far call into a shim becomes `int / db` (3 bytes against 5), and each shim body (4 bytes) becomes a 2-byte word. **Census [s]: 45 shim bodies and 23 zero-byte aliases** (KERNEL-BYTE-QUEUE's 2026-11-08 status line; `build/kernel.map` lists 70 `cw_*` symbols). The aliases stay far calls, because a table word would cost them +2 | Nothing shipped: modules are versioned with their kernel | About **-45 to -90 `.text` kern_big** (-90 for the bodies, less the door's ~45 if T5 is not yet in), plus -2 a site in `.cold` (resident) and in module images. **Correction [s]:** gfr-2 said -140 from 70 symbols. Cost about +170 clocks a call on module paths (fm, desk, ctrl), which are cold by construction. It also sidesteps KERNEL-BYTE-QUEUE item 1's BP-liveness gate, because the door clobbers no register | none (kernel-private) | gfr-2 [g], bytes-7 [c], KERNEL-BYTE-QUEUE item 1 | **proposed - NEEDS NO FLAG DAY; take it BEFORE T5.** It proves the DOS-vector rule, the IF rule and kern_mega's gate on traffic nothing else depends on |
| **P5** | **One boot handoff record.** Stage 2, `boothd` and the KZ door hand the kernel DL, BX:CX, BP and `[spl_fseg]`, and what each means depends on which loader ran. A 16-byte record at a fixed low address (0060:0004..000F is the precedent) holds drive, partition base, spt, heads, t0, loader id and cylrun | The loaders and the kernel together: an internal flag day; they ship on the same disk | About neutral. The contract becomes a structure a test can read | none (private) | boot.md Rethinks (boot-5, boot-13) | **recorded** |

### 13.A Additions priced (no bump needed)

| id | what | buys | bump | source | status |
|---|---|---|---|---|---|
| **A4** | **A run-time API LEVEL: PKG_FMT's minor number.** <br>- Kernel: `dw OS88_API_LEVEL` at 0060:0006, free padding today (`kernel.asm:2357-2359`), 0 resident. <br>- Package: `os88pkg.py` writes the needed level, numbered as The Wire's `WIRE_CELL_LEVELS` already numbers them (`os88wire.py:120-138`), into header byte 31. It is 0 in every package built, because `os88pkg.py:122` refuses a 16-character name. <br>- Load: `ld_check_hdr` refuses a level above the kernel's with `LD_EVER` (+~8 `.cold`). <br>- SDK: `OS88_NEED` for a run-time test. <br>**A level, not `osapi_table_end`**: T5 takes the rare cells out of the table, after which a table end would stop describing the API. It also refutes abi-16's "no header room" for this purpose; abi-16's extension-list bit is not needed at this flag day. Before reusing byte 31, check the other readers of the 16-byte name (mount harvest, Sticky Apps), and confirm that no PKG_FMT-10 kernel predates the debug registry that moved a word off 0060:0006 | Load-time refusal of a package that needs newer cells, the hole SPEC.md 20.2.0.1 names with no remedy. GEOS protocol numbers and AmigaOS `OpenLibrary(name, version)` are the precedents. T5 adds the run-time half | none | abi-6 [c], loader-6 [k], gfr-5 [g], abi-16 [c] | **proposed - NEEDS NO FLAG DAY; take it first**, since it protects only kernels from then on |
| **A5** | **The appended cells the flag day makes cheaper, recorded so none is held back for it:** <br>- sched-15's `task_wake` (+24 to +30, plus a cell); <br>- dr-5's `FILE_FIND_SEQ` (+45 `.cold`, plus a cell); <br>- hm-9/mem-15's purgeable package claims (+30 to +70 `.cold`); <br>- ui-11's double-click byte (+5, plus an inline cell); <br>- ui-12's clipboard generation count (+6, plus a cell); <br>- ui-19's `W_ONMENU` (+14 `.text`, +24 `.bss`, plus a cell); <br>- video-12's `FSIF_FLIP` (+8 / +4); <br>- shell-3's `AH = 0FEh`; <br>- D8's `OSAPI_IRQ` | Under T5 an appended rare cell is 2 bytes, not 6. Under R14 a new callback is a class slot and 0 `.bss`. Neither changes whether an addition is worth taking now | none | sched-15 [c], dr-5 [c], hm-9 [k], mem-15 [c], ui-11 [c], ui-12 [u], ui-19 [u], video-12 [c], shell-3 [c] | **recorded** |

### 13.V Speed

| id | what changes | who it breaks | what it buys (cycles, ms, bytes) | bump | source | status |
|---|---|---|---|---|---|---|
| **V7** | **T5's renumber sorts by MEASURED call frequency.** Under T5 a rare call costs about +150 to +215 clocks over a SLOT (about +78 today), and promoting a cell costs +6 bytes (+2 today), so the hot/rare line has to be drawn with a counter, not by today's shape. Promotion candidates: <br>- `OSAPI_SEG` (RCXCELL 0x04E3, 108 sites [s]): kern_mega's derived-segment door, possibly called per access; <br>- `OSAPI_FS_OP` (RSLOT 0x04EF, 45 sites [s]): a per-step progress call. <br>Demotion candidates: K17-4's thirteen not-hot SLOTs (-78 under T5). V2's yield cell enters directly in the same renumber. V3's XMEM trio stays rare (7 sites). **Correction [s]:** gfr-1's `OSAPI_GFX_FILL_GRAY` is already a hot SLOT (0x0048) | Inside T5's renumber | It avoids about 40 us a call on any cell that turns out hot. +6 a promotion, -6 a demotion. The reading costs one rebuild: a counter in the door (PERFORMANCE.md rule 4) | inside T5's | gfr-1 [g], bytes-1 [k], K17-4, V2, V3 | **proposed - the reading is a prerequisite of T5** |

---

## 3. The recommended flag-day bundle

### 3.1 What has to be true before the flag day (Phase 1; no bump)

These are Phase 1 work, listed here because the flag day depends on them. Each
is either a gate the flag day needs or a half of a row that does not need the
break.

| item | why the flag day needs it | bytes |
|---|---|---|
| **abi-3 / gkm-2**: `t_api_abi` over all four kernels | It decodes kern_big only. A renumber verified on one kernel of four is not verified; under T5 it must also decode the rare index table and `CD nn idx` sites | 0 (host) |
| **engsys-11**: `apps/cc/os88.h` and `os88thunk.asm` into PKG_FMT's fingerprint and `t_mirror` | Section 0 item 4 says the C SDK moves in the same commit, but no gate fails when it does not | 0 (host) |
| **drivers-16** (P2): VX/XM/HD versions fingerprinted | A flag day that moves a private ABI then notices | 0 (host) |
| **abi-8**: machine-readable `@ctx` tags on each contract | Lets the rebuilt tree be checked against them, and answers T5's IF question | 0 shipped |
| **abi-4**: `API_RN_DSOFF` | T5 moves the frame by FLAGS; the literal must be one equate | 0 |
| **A4**: the API level | Every kernel from then on refuses a too-new package; the level then survives T5's table reshape | +~8 `.cold` |
| **T6**: the `OSCALL` rewrite, byte-identical | T5 becomes a macro flip; proved by `cmp` of every image now | 0 |
| **V7's reading**: a counter in the rare path | The renumber's sort | 0 shipped (one rebuild) |
| **P4**: the door on the `cw_` shims | Proves T5's door on kernel-private traffic | about -45 to -90 kern_big |
| **N1**, **histsys-7** | D4 completes them at the bump | -11 every driver kernel; -90 to -140 kern_big, -36 kern_small |
| **L7** (clean bit), **mc-10** (`[mega_kfix]`) | L3's staged road | +12, +8 `.mega` |
| **D8 phase 1** (`OSAPI_IRQ` appended) | Lets the flag day make it mandatory | ~+100 kern_big (the owner's call) |
| **F3 phase 1** (dual-mark reader) | Lets the flag day drop the old mark | +6 `.cold` |
| **shell-3** (`AH = 0FEh`), **abi-9** / **gfce-8** (doc and class table) | Additive halves of C10 and R9 | +12 CTRL.DRV, 0 |
| **abi-15 / engsys-20 / gfce-9**: an `APICHK=1` knob | The flag day's soak can then run a checked kernel once, so every contract violation in the rebuilt tree is reported against the package that made it | 0 shipped |

### 3.2 The bundle, in order

The bump is **one `PKG_FMT` 10 -> 13 and `DRV_VER` 12 -> 14** (section 0,
item 2). It is one series of commits on one branch, and the C SDK moves in the
same commit as the SDK. The order is chosen so that **the table is renumbered
once**:

1. **Withdrawals first**, so that nothing is renumbered twice: C1 (the six KB
   memory doors), C2 (APPEND), C4' (APPEND_SYS alone), R1's bytes form, and
   R14's setters. Also any K17-4 demotion V7's reading supports.
2. **R14**: the class table, the `WT_*` template reshape and K17-2's one
   `OSAPI_WM_FLAG` door. This also deletes six of T2-2's kern_mega ICW stubs.
3. **Re-contracts**: R2 (snapshots bounded by the caller), R3 (volume bridge
   in ES:DI), R9 (one refusal shape), R12 (display index), R15 (clipboard
   format), C10 (desk item by record), R2-4 and R2-5 (FSX_SPK and FSXF_REAL),
   V1's new rect entries, and D4 (the service-table floor). Optional: R16 (the
   Open filter) and D8 made mandatory.
4. **T5**: sort the rare set by kind and by V7's measured frequency, renumber
   once, and flip `OSCALL`'s rare arm to the door. V2 rides it. T6's renames
   land here with one cycle of aliases.
5. **L1**: `KERNEL_SEG` 0x0060 -> 0x01A0, the FAT window and `dsk_secbuf`
   below the image. It comes after T5, so that T5's `t_api_abi` diff is not
   confounded by every address moving, and because after T5 L1's churn is only
   the hot SLOTs, ES-on-callback, `crt0.asm`, DOS, the loaders, the ROMs,
   `COLDHMA.SYS` and the tools. kern_mega: it swaps which GDT identity entry is
   the readable-code one; measure kern_mega's own post-switch pad before quoting
   its saving.
6. **Files on disk**: F1 (`'VC'` folded, one-time loss of Force Color) and F3
   (the old dirent mark dropped). kern_mega only: R2-1 (`mega_hibnc`, -223
   `.mega`) and R2-3 (`MEGA_WTN` 32 -> 64).
7. **The decision and the gates**:
   - `abilock.py --update` with both bumps;
   - The Wire's level generation reset at 13 (A4's level is its level), and the
     catalogue republished;
   - `t_api_abi` on four kernels;
   - `make budget-check` on all four budgeted kernels;
   - then the soak, which is the owner's to ask for, ideally on an `APICHK`
     kernel.

### 3.3 What it saves or costs, per kernel

Resident bytes, signed (big / small / mega conventional), ESTIMATED unless
marked. A dash means the row does not reach that kernel.

| row | kern_big | kern_small | kern_mega conv |
|---|---:|---:|---:|
| **L1** (today's pad, MEASURED; 0-511 by build) | -386 | -309 | -232 |
| **T5** + two promotions (V7) | -385 | -245 | -385 |
| **R14** + K17-2's door (under T5) | -215 | -95 | -270 |
| **C1** KB doors withdrawn (under T5) | -45 | -45 | -12 |
| **C2** APPEND withdrawn | -11 | - | -2 |
| **C4'** APPEND_SYS withdrawn (MEASURED spans) | -17 | -3 | -12 |
| **R1** REHOME KEEP form | -16 | - | -6 |
| **R2** snapshots bounded by caller | +6 | +6 | +6 |
| **R3** volume bridge | -4 | - | -4 |
| **R9** one refusal shape | +5 | +5 | +5 |
| **R12** one display index | -6 | - | -6 |
| **R15** clipboard format | +8 | +8 | +8 |
| **C10** desk item by record (`.cold`) | -6 | - | - |
| **D4** service-table floor | -20 | - | ~-20 (the `.text` part only) |
| **F1** `'VC'` folded (+ -5 `.boot2`, transient) | -1 | - | -1 |
| **F3** old dirent mark dropped (`.cold`) | -6 | -6 | - |
| **V1** clobbering rect entries (-8 kernel, +6 entry) | -2 | -2 | -2 |
| **V2** yield enters directly | 0 (-60 clocks a yield) | 0 | 0 |
| **R2-4 / R2-5** FSX_SPK and FSXF_REAL | -3 | - | -25 |
| **T6** names | 0 | 0 | 0 |
| **net** | **~-1,100** | **~-690** | **~-960** |

How to read the total:

- **With L1 at its ~256 average** rather than today's pad, the net is about
  -970 / -635 / -985.
- **kern_mega also drops -223 `.mega`** (R2-1), which is above 1MB.
- **Optional rows are not in the figure:** R16 (+30 `.cold` kern_big; on
  kern_small it lands in FDLG.DRV, not resident) and D8 made mandatory (-25
  `.cold`, after Phase 1's ~+100).
- **Phase 1's own net** comes before all this:
  - kern_big: about -130 to -230 (N1 -11, histsys-7 -90..-140, P4 -45..-90,
    A4 +8, F3 +6), and about 100 less of a saving if D8's cell is taken;
  - kern_small: about -20 (histsys-7 -36, A4 +8, F3 +6);
  - kern_mega: +20 `.mega`, with its conventional share not separately
    priced.

**Packages and drivers** (unpacked; packing shrinks every figure):

- **T5: -2 a rare site, 2,736 sites, about -5.5KB in all.** The largest:
  WIN16 -378, RealDMG -266, Pixel -246, DOS -224, XTMP3 -194, Video -180,
  Browser -166, Arcade -156, HDD.DRV -146, Scribe -144. A C package saves up
  to about -130, through the 65 rare sites in the thunks it links.
- **R14: about -1.5KB net** across windowed packages.
- **C3 is not taken**, which avoids its +300.
- **R15: up to +78** across the clipboard callers.
- **R16 (if taken): about +2** at each Open call.
- **D4:** HDD.DRV and EMS.DRV pad their tables, on their own disks.
- **C10:** WIN16 -12.

### 3.4 What stays out, and why

| row | why it is out |
|---|---|
| **L3** | Blocked on The Wire serving kern_mega builds (the owner's ranking). L7 and D8 start it now |
| **T1 / K17-1** | The fallback for T5 |
| **K17-3** | Superseded by R14 |
| **K17-4** | Only where V7's reading supports a demotion |
| **K17-5 / C7** | Recorded |
| **T2-2** (the four left after R14) | Refused: XT cycles against about 32 conventional bytes |
| **C3** | Refused, by arithmetic |
| **D1 / drivers-9** | Moot under histsys-7 + D4 |
| **R6 and wm-R3** | Shelved together |
| **R2-6 / hm-10** | Recorded (gfr-4's correction) |
| **R10, R11, L6** | Each has an additive form, so it needs no flag day |
| **R13** | Only if C1 converts the Task Manager anyway |
| **C9, D5, D6, D7, P3, P5** | Recorded |
| **V3, V4, V5, A1** | Unchanged: recorded, or an addition waiting for a consumer |
| **F2** | Recorded (a user-visible rename) |
| **P1 and P2-1** | Private: ride any DOS release or any kernel pass, not this bump |

### 3.5 Open questions for the owner

1. **T5's door.** Is a software interrupt acceptable for the rare half, at
   about +30-45 us a rare call, with a vector the kernel must own against
   windowed and full-screen DOS? If not, T1 is the table row.
2. **The IF rule.** Is any rare cell legitimately called with IF=0? abi-8's
   tags answer it. "IF=1 callers only" saves about 30 clocks a rare call.
3. **R14 and the C SDK.** C packages set handlers through `os88.h`
   prototypes. Should the C SDK emit a class table from the setter calls it
   sees, or should C packages declare one?
4. **D8.** About +100 resident on kern_big now, for IRQ ownership on every
   kernel. That is the owner's call (isr-13).
5. **L3 as product.** gkm-9: is a one-disk 8088/286 set a goal? If it is, L3
   is a product row, and L7 is where it starts.

## Sources

- **Area data:** `docs/reports/big-review-2026-11-08/data/{abi,bytes,diskwrite,drivers,files,gfx,histmem,histsys,isr,loader,megacore,megados,memory,sched,shell,sysmisc,ui,video,wm,engsys,input,text,diskread,hiber,boot}.json`.
- **Area reports:** `areas/{abi,bytes,histsys,drivers,megacore,ui,boot,wm,shell,files,video,memory,histmem,isr,loader}.md` (findings and Rethinks).
- **Gap reports:** `areas/gap-flagday-reconcile-abilist.md`, `gap-fault-containment-error-model.md`, `gap-context-lock-model.md` and `gap-kernel-matrix-near-model.md`.
- **Plans:** `docs/plans/ABI-BREAK-LIST.md` (all) and `docs/plans/KERNEL-BYTE-QUEUE.md` (status line, item 1).
- **Tree checks [s]:**
  - `build/kernel.map`, `build/smallk/kernel.map` and `build/megak/kernel.map`: section summaries and the `append_sys` symbols;
  - `kernel/kernel.asm` (table lines 2660, 3315, 3558, 3715, 4407, 4423; `FAT_SEG` 1804);
  - `kernel/driver.inc` (2970-2992, 4040-4100);
  - `kernel/instance.inc` (2093-2118) and `kernel/sched.inc` (2868-2885);
  - `kernel/loader.inc` (`ld_rehsz`);
  - `drivers/os88drv.inc` (`OS88_DRIVER`);
  - `apps/dos/dosw.inc` (75-80);
  - `kernel/lz.inc` and SPEC.md 20.13.9;
  - a `call OSAPI_*` census over `apps/` and `drivers/`, with rare cells resolved through the table's address comments.
