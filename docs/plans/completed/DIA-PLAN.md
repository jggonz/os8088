# DIA-PLAN: Desktop Integrated Applications, and the Desk Calendar

**Status: BUILT** - waves 0 to 4 on `feature/dia` (SPEC.md §107 is the
contract; this file is the design record it was built from). Wave 5, the
desk LAYER, was owner decision D1 and is NOT built (§107.8). "§" means
SPEC.md throughout.

This is the judged and synthesized design exactly as it was taken into the
build - three candidate designs scored, one corrected, four ideas taken from
the runners-up - followed by **every place the code proved it wrong**,
wave by wave, as the builders recorded them. Read the design for WHY the
shape is what it is; read the log at the end, and §107.9, for what actually
ships where the two differ. Byte counts in the design are its estimates; the
measured ones are §107.6's.

## As built, in one table

| the design said | what shipped | where |
|---|---|---|
| K1-K6, ~+6 `.text` / ~+104 `.cold` on kern_big | K1-K6, **+6 / +116**, and **+6 / +32** on kern_small; no rung crossed; nothing in waves 2-4 | §107.6 |
| `dia_boot_x`, far | `dia_boot`, near (both ends `.cold`) | §107.9 |
| a save is one full `OSAPI_FILE_GOTO` per path component | one full mount, then `OSAPI_FILE_GOTO_QM`; and it asks whether the disk is still HOME first | §107.5.1 |
| register only on `OPEN = 0` | register on EVERY launch, whatever the state file said | §107.5 |
| the day field `dd*`, glyph 7 as a bullet | `[mark][d][d]` with a `*` (glyph 7 is outside the kernel font) | §107.3 |
| agenda 5 lines at pitch 10, a tab row on COMPACT | 7 / 4 lines at pitch 8, no tab row; the entry field takes a heading's place | §107.3 |
| the layout chosen from `OSAPI_VIDEO`'s height | chosen from the CONTENT height the kernel gave the window | §107.3 |
| `wm_fit` clamps "whatever is left" | the template is sized for the adapter BEFORE the create, and `dia_made` gives a pulled-up `y` back | §107.5.2 |
| a save at the next 2 s tick | held while the user is mid-edit (`DIA_ONTICK`'s CF), so a burst is one save | §107.5 |
| the chirp 880 then 660 Hz everywhere | the same on kern_big through a once-only sooner beat (`dia_nx`); ONE 880 Hz note on kern_small's worker arm | §107.3 |
| missed = due-but-unfired at launch | missed = due BEFORE this minute; a moment already past (before this minute) when ARMED is fired silently, one armed for this minute fires at the next beat; nothing fires under an open entry field | §107.3 |
| "set FIRED and dirty" | the FIRED marks owe saves the user did not cause - a launch that missed reminders, an unattended firing - and they are priced; an alert holds no save | §107.3, §107.5.2, §107.6 |
| `deskcalremind`'s QEMU `make test-snd` + `sndcheck.py` arm | the tone asserted at `OSAPI_SND_TONE`'s registers on MartyPC, not off the speaker | §107.9 |
| `Clock not set (no RTC)` | `No RTC - check the time` (the cell cannot say whether it was set) | §107.3 |
| §9's `make diashot` fixture and the PR's screenshot set | not built in waves 0-4: the 1bpp pictures are `tests/deskcal1bpp.py --shots`'s, and the gates assert on pixels and on the state file instead; the set belongs to the pull request | §107.9 |

The rows that hold it: `t_dia` (fast); `diaboot`, `diamiss`, `diashort`,
`diahiber`, `diaswap`, `diaregister`, `diamove`, `diamovecga`, `diaanchor`,
`deskcalgrid`, `deskcaledit`, `deskcaltask`, `deskcalremind`, `diasmall`,
`deskcalperf`, `deskcal1bpp` (soak) - §107.10 says what each asserts.

---

## The design, as judged

---

## 0. Verdict in one paragraph

**The winner is `zero-kernel`, with one fatal flaw corrected and four ideas taken from the runners-up.**

What the user gets:
- A DIA is an ordinary package in `A:\SYSTEM\DIA\` that includes a new shared source file, `apps/os88dia.inc`.
- "This widget is open, launched from here" is stored as one row in the existing SYSTEM.CFG desktop trailer. That trailer is how desktop icons are already persisted. The row is written through the existing `OSAPI_DESK_ITEM` (0x03F8) into the existing on-demand settings core (CTRL.DRV), which is §2.8's mechanism, the one DOCK.DRV uses.
- At boot, a ~60-byte loop in `.cold` relaunches each marked row through the existing `sc_open`. A missing file produces one toast, and the row turns back into a visible desktop shortcut.
- Position and calendar data live in the widget's own `SYSTEM\APPDATA\DESKCAL.DAT`.

What it costs, resident:
- **kern_big ≈ +6 `.text`, ≈ +110 `.cold`, 0 `.bss`, 0 `.ovl`.**
- **kern_small ≈ +6 `.text`, ≈ +30 `.cold`.**
- No new module, driver, SYSTEM.CFG key, or PKG_FMT/DRV_VER bump.

What it does not do yet: v1 is a normal window. A true desk layer (behind other windows, no raise, no dock tile) is a separately costed **wave 5** that needs the owner's decision (§11, D1).

---

## 1. Scores

| criterion (0-10) | zero-kernel | user-first | risk-first |
|---|---|---|---|
| Fidelity to the request (minimise kernel, dock-like split, reuse desktop-icon code, DIA folder, free placement, restore, graceful failure) | **8**. Reuses the icon trailer and CTRL.DRV; free placement is a window drag; restore and failure both handled. Loses points on widget feel. | **7**. Best widget feel (desk layer, no chrome, no tile), but ~300 resident bytes in hot paths runs against "minimize or eliminate". | **7**. Copies the Dock vehicle literally (new DIA.DRV); uses the icon code only for an optional "Put on Desktop". |
| Correctness against the code | **5**. One fatal bug (+3 is not spare, §2.1) and one stale-state bug (§2.2). Otherwise its claims check out. | **6**. Kind-3 overlay work identified correctly; the Discard lock problem caught. `CLK_GET` via `clk_snapshot` steps on the UI formatters' copy; the ten-site `WF_FULL` widening is under-analysed. | **5**. Its central cell `OSAPI_DIA AL=0` rests on a stale rule (§2.3); otherwise careful. |
| Kernel bytes, big / small | **9 / 9** (≈116 / 36, corrected) | **3 / 9** (≈301 + .ovl / 34) | **5 / 9** (≈26 text + 110 cold + 10 bss + module plumbing / 36) |
| Testability | 8 | 7 | **9**. Fixture disks, and a proof that the stock machine pays nothing. |
| Build risk | **8**. No overlay, no module plumbing. | 4. Image rung has 200 B left; drag cache, raise paths and `WF_FULL` sites. | 5. New `MOD_*`, five tool section lists, `KMODARGS`, a CFG struct byte. |
| **Overall** | **7.6** | 5.4 | 6.2 |

---

## 2. What the spot-checks found (claims that are wrong)

### 2.1 FATAL in zero-kernel: SC row byte +3 is not spare in practice [verified]

- `kernel/desksc.inc:452-454`, in `sc_m_new` (the drag-a-file-out-of-a-Disk-window path), says: *"`add di, SC_R_PATH - 1` — ONE BEFORE THE FIELD, so dsk_path's leading `\` lands on the spare byte"*.
- So **every shortcut a user has ever dragged out carries `0x5C` (`\`) at +3**, in memory and in every SYSTEM.CFG on every user's disk.
- SPEC §26.8.1's table (SPEC.md:50620) still calls +3 "spare". That is SPEC drift to fix in wave 0.
- Zero-kernel's `desk_live` test, `cmp byte [+3],1 / cmc`, treats any non-zero +3 as not live. It would **hide every existing drag-made shortcut on boot**.
- Its writer rule ("reset +3 values other than 0 or 1 to 0") would then rewrite them.
- **Fix used below:** a DIA row is +3 == **`'D'` (0x44)** exactly, plus KIND == 1. `'\'` and `0` (the only values the tree writes today) never match. `desk_live` tests equality, and the writer leaves +3 alone. The package-add path (`sc_m_add`) zeroes +3 for any record that is not a DIA record, so a junk +3 from an old package's record cannot become `'D'`.
- **Kind 3 (user-first) was the other way out.** It costs `.ovl` bytes in `ovl_sc_load`: its `cmp ah,2 / ja .kill` turns kind > 2 into a free row (`desksc.inc:1291-1297` [verified]). The blob has **26 bytes left** on kern_big [measured: `BOOT2_SECS`·512 − `OVL_AT` − ovl = 5,120 − 2,624 − 2,470], less on kern_emu and on knob builds. The `'D'` mark costs no `.ovl` byte at all, because `ovl_sc_load` copies +3 verbatim (`rep movsb`, :1263-1270 [verified]) and `sc_m_add` copies it with `rep movsw` (:551-565 [verified]).

### 2.2 zero-kernel: the DAT's OPEN flag goes stale

Zero-kernel registers only when the DAT says `OPEN = 0`. Here is the failure sequence:
1. A boot finds the file missing and downgrades the row.
2. The DAT still says `OPEN = 1`, because the package never ran.
3. The user later relaunches the widget by hand.
4. The widget sees `OPEN = 1` and never registers again, so it never autostarts again.

**Fix:** register on every launch. CTRL.DRV's DIA add is idempotent and **writes nothing** when an identical marked row already exists (user-first's rule). The price is one settings-core read per launch, after the window is up: 5 sectors, 1-2 `int 13h` calls.

### 2.3 risk-first: "an entry proc reads no file" is stale [verified]

- `kernel/loader.inc:1120-1137`: the entry proc is now **stamped with its instance**. The comment says that before the stamp, "the file API answered with the machine's directory instead of the instance's". That has since been fixed.
- `apps/os88api.inc:746` lists file calls as legal from "the entry proc, a window callback, a menu handler or a file-dialog completion proc".
- §67.20 and the `cyclone.asm:1106` note are Cyclone's own history, written before the stamp.
- So the widget can read its own DAT in the entry proc and place the window before `OSAPI_WM_CREATE`. **`OSAPI_DIA AL=0` (the staged-position cell) is unnecessary**, and so is the 'DA' gate byte.

### 2.4 Other checks

| claim | status |
|---|---|
| `0x0467` is the next free API cell | **Confirmed**. `osapi_table_end` is the label at `kernel.asm:4545`. The length assertion is at :4561 (`98*6` rare cells); one more RCSLOT makes it `99*6`. |
| Clock field layout | **Confirmed**. `clk_sec, clk_min, clk_hour, clk_day, clk_mon` are bytes in that order at `clock.inc:2177-2181`, so `[clk_min]` read as a word is BL = minute, BH = hour, and `[clk_day]` as a word is DL = day, DH = month. `clk_year` is a word (:2187); `clk_rtc` is at :2185 and `clk_h12` at :2201. Only `clk_mon - clk_day` is asserted (:2224), so wave 1 adds `clk_hour - clk_min == 1` to that `%if`. |
| `api_rsc` reaches a `.cold` body | **Confirmed** (`kernel.asm:4693-4702`). It is reached with `DS = KERNEL_SEG` through `api_far`, so the body ends in **`retf`**. |
| `drv_notice_x` context | **Confirmed**. It runs after the first paint, with no lock, as the last thing before `jmp ui_task` (`kernel.asm:6018`, `driver.inc:4618-4646`). kern_small's is a bare `retf` (`driver.inc:5304`). |
| `sc_open` | **Confirmed** (`desksc.inc:114-187`). It walks any depth and launches by `ldf_ld_pkg_start`; a miss toasts `Shortcut not found (X:)` and returns with no defined CF; a load refusal goes through `ld_say_status`; `drv_vol_back` preserves FLAGS (`drvvol.inc:133/156`). |
| `osapi_desk_item_x .pdel` passes AH = 0xFF through | **Confirmed** (`desk.inc:2185-2190`: it refuses only AH < `DESK_SC0`). kern_small's body is `stc / retf` (:2213). |
| `sc_modcall` lock and refusal | **Confirmed**. It documents "the gfx lock held" and on a missing module says `Needs Sys Disk A:` through `hbf_nodisk` (:189-227). |
| Content click on a background window | **Confirmed**. It is raised and the click is swallowed (`ui.inc:456-462`). |
| Hibernate question posted before ui_task | **Confirmed**. `hb_probe_x` posts `UI_RBQ_ASK` (`hiber.inc:341`) and step 0 of ui_task spends it (`ui.inc:85-98`). `hiber.inc:420` can also post `UI_RBQ_DOSRUN`. "`[ui_rebootq]` ≠ 0 → stand down" covers both. |
| Discard runs with the gfx lock **held** | **Confirmed** (`hiber.inc:1113-1150`, `hbm_act`). So user-first is right: a Discard re-launch has to be **posted** (`CMD_DIA`), not called. |
| Section numbering | §106 is the highest used top-level section and §104 was never used. **Take §107.** No section registry exists under docs/. |
| Rung headroom [measured, `tools/kernsize.py`] | kern_big image rung **200 B left**, cold rung **462 left**, low 34, footprint 59 steps spare. kern_small was not measured (no `build/smallk` in this worktree). |
| `OSAPI_WM_OWNSEG` (0x03BF) | **Exists**. It answers DX = the segment owning window slot AL, so a package-side singleton is possible. |
| `TESTIMG` | **Exists** (`Makefile:13667`). `make test TESTIMG=<img>` boots a chosen A: disk. That is what the screenshots use. |
| `-rtc base=` | **Not used** anywhere in the tree, so `tests/os88qemu.py` takes no extra args today. Date pinning goes through MartyPC instead, poking `clk_*` the way `tests/kdreturn.py:387` does. |

---

## 3. The design

### 3.1 Definition and vehicle

A **DIA** is an ordinary `.O88` package: `OS88_HEADER`, an `OS88_F_ICON` 16×16 icon, and one window. It has three properties:
1. It lives in **`SYSTEM\DIA\`** on the boot volume. This is shipped by the build; DIA has no header flag and no registry file.
2. It includes **`apps/os88dia.inc`**, shared as source in the `os88ui.inc` / `os88line.inc` shape. That include owns position restore and save, presence registration, the state file header, and the tick multiplexer.
3. While it is open, the **desktop configuration** (the SYSTEM.CFG trailer, §26.8.6) holds one **DIA row** for it, and the kernel relaunches it from that row at boot.

**Why this is "the dock lesson".** DOCK.DRV is a §2.8 on-demand module (`MOD_DOCK`, `mod.inc:46`). It is not a §51 driver. The lesson is: keep the resident part to tests and thunks, and put everything that touches a file into a module read from the system disk. Here:
- The file-writing logic (add, update, remove the row, and persist SYSTEM.CFG) goes into the **already existing** settings-core module, CTRL.DRV's `.modc` / `MOD_SETS`, which already owns that trailer.
- What stays resident is a liveness test, a CF bit and a 60-byte boot loop. That is less than one new module's plumbing (~15 B `.text` + 4 B `.bss` + a ~26 B caller), and a new module would also cost a disk read on every boot just to learn there is nothing to do.
- A §51 **driver cannot do the job at all**. Drivers attach in `drv_boot_x` before `ui_task` exists. They cannot reach `OSAPI_PKG_START` (os88api.inc:1138-1148), and their workers may not touch files (§20.6 rule 7).

### 3.2 Kernel changes (exact)

**K1 — `OSAPI_CLK_GET`, RARE cell at `KERNEL_SEG:0x0467`, on both kernels.**
- `kernel/kernel.asm`: add `OSAPI_RCSLOT osapi_clk_get_x` after the `0x0461` cell (:4535). `osapi_table_end` becomes 0x046D. Change the assertion at :4561 to `99*6` and fix its message.
- Body in `kernel/clock.inc`, `section .cold`:
  ```
  osapi_clk_get_x:          ; far, DS = KERNEL_SEG (api_rsc)
      pushf
      cli
      mov cx, [clk_year]
      mov dx, [clk_day]     ; DL = day, DH = month
      mov bx, [clk_min]     ; BL = minute, BH = hour
      mov ah, [clk_sec]
      mov al, [clk_h12]
      shl al, 1
      or  al, [clk_rtc]     ; AL bit0 = hardware RTC found, bit1 = 12-hour display
      popf
      clc
      retf
  ```
- Contract: out `CX` = year, `DH` = month 1-12, `DL` = day, `BH` = hour 0-23, `BL` = minute, `AH` = second, `AL` = flags; CF = 0; every other register preserved.
- Context: **UI task** (entry proc, window callback, ONWAKE). The UI task is the only writer (§37), so a read from the UI task is always consistent. A worker may call it and accept a torn read across a rollover.
- It reads the live fields under `cli`. It deliberately does **not** call `clk_snapshot`, whose `clk_sn_*` copy belongs to the menu-bar formatters (`clock.inc:1853`).
- Add the assertion `clk_hour - clk_min == 1` to the `%if` at :2224.
- SDK: `%define OSAPI_CLK_GET KERNEL_SEG:0x0467` and its contract in `apps/os88api.inc`. Then `python3 tools/abilock.py --update "OSAPI_CLK_GET appended at 0x0467"` with **no PKG_FMT bump**, since the cell is appended at the end (§20.2.0.1).
- Cost: **.text +6, .cold ≈ +24, on both kernels.** kern_small has the PIT clock (§37.0.1); `clk_rtc` = 0 there.
- It is unavoidable. The SDK has no date at all (`apps/dos/dos.asm:13563`), and the debug block is closed to shipped code (§57.3 rule 1).

**K2 — the DIA row mark (constants only).**
- `kernel/desksc.inc`: `SC_R_DIA equ 3` and `SC_DIA equ 'D'`. Rewrite the record comment at :38-40 to say: "+3: `\` on a dragged shortcut (sc_m_new's dsk_path spill), 0 on a package's link, `'D'` on a DIA row".
- `apps/os88api.inc`: `OSAPI_DI_DIA equ 3` and `OSAPI_DIA_MARK equ 'D'`.
- `t_mirror` holds both pairs equal.

**K3 — `desk_live` hides a DIA row** (`kernel/desk.inc:624-634`, `.link` arm, `.cold`, `%ifdef OS88_SHORTCUTS`). The sequence after `call sc_row` becomes:
```
    call sc_row                 ; CF = 1 free/none
    jc .ld
    cmp byte [es:bx+SC_R_DIA], SC_DIA
    stc
    je .ld                      ; a DIA row: not live, no cell, never painted or hit
    clc
.ld:
    sbb al, al
    inc al
```
- **.cold +11, kern_big only.**
- Rule 1 of `desk_reflow` (desk.inc:813) already lets a non-live row hold no cell. The row's zslot byte is 0xFF, so it never takes a cell.
- This must stay resident: it sits on the reflow and paint path, which may never touch a disk (§30.5).

**K4 — `sc_open` answers CF** (`kernel/desksc.inc:170-187`):
- add `clc` after `call ld_say_status`: a load refusal is not "not found";
- add `stc` before the final `ret` of `.nf`, after the toast.

New contract: CF = 1 iff the walk found no volume, folder or file. **.cold +2.** `desk_open`'s `jmp sc_open` ignores it.

**K5 — `dia_boot_x`**, new `.cold` far routine in `kernel/desksc.inc` beside `sc_open`, `%ifdef OS88_SHORTCUTS`:
```
dia_boot_x:                     ; far (retf): kmain via drv_notice_x, and wave 5's CMD_DIA
    push ax / push bx / push cx / push es
    cmp byte [ui_rebootq], 0
    jne .r                      ; resume/DOS question pending: the image IS the session
    mov cl, [sc_nrows]
    xor ch, ch
    jcxz .r
    mov al, DESK_SC0
.l: call sc_row                 ; ES:BX = row
    jc .n
    cmp byte [es:bx+SC_R_DIA], SC_DIA
    jne .n
    cmp byte [es:bx+SC_R_KIND], 1
    jne .n
    push ax / push cx
    call sc_open                ; NO lock held - kmain's context
    pop cx / pop ax
    jnc .n
    call sc_row                 ; re-read: the load may have moved [sc_seg]
    mov byte [es:bx+SC_R_DIA], 0    ; DOWNGRADE, in memory, to the shortcut it is
    call desk_reflow            ; it takes the first free cell, is pinned, is posted
.n: inc al
    loop .l
.r: pop es / pop cx / pop bx / pop ax
    retf
```
- Call site: the first instruction of the kern_big `drv_notice_x` (driver.inc:4637) is `call COLD_SEG:dia_boot_x`. It goes there, before the Control Panel notice, so that a driver-failure panel still opens **in front** of the widgets.
- **.cold ≈ +62 (body) +5 (call), kern_big only.** kern_small's `drv_notice_x` is the `retf` stub and pays 0.
- A machine with no DIA row pays one byte compare and a `jcxz`, and does no disk I/O.
- This is the context `OSAPI_PKG_START` demands: task 0 becomes `ui_task`, the lock is free, the desktop is painted, and A: holds the boot disk (driver.inc:4642-4646).
- **Wave 1 must check that `desk_reflow` is safe with no lock held.** It only rewrites `desk_zslot` and posts `desk_cdirty`, and its driver-half caller precedent is `osapi_desk_item_x`. If it is not, the downgrade sets `[desk_zdirty]` and the ui_task tail does the reflow instead.

**K6 — CTRL.DRV settings core: the DIA rules** (`.modc`, not resident).

`sc_m_add` (desksc.inc:551):
1. On entry, peek the caller's record at DX:CX.
   - If +3 ≠ `'D'`, store +3 = 0 after the copy, so no other value can masquerade as a mark.
   - If +3 = `'D'` and KIND ≠ 1, refuse (`.bad`).
2. **DIA add-or-update.**
   - Scan the live rows for one with +3 = `'D'` whose path's **final component** equals the record's.
   - If that row is byte-identical in VOL and PATH, answer AL = its zone and **write nothing**.
   - If it differs (a launch from a new place), copy the record over that row in place, so "where it was last launched from" wins.
   - Otherwise claim a row via `sc_m_row`.
3. For a DIA row, **skip the `desk_free` refusal** and store zslot **0xFF** (no cell, never shown) instead of `wish|DSL_PIN|DSL_GONE`.
4. `sc_m_save` writes SYSTEM.CFG through `cp_flush_cfg`, as it does today.

`sc_m_pdel` (desksc.inc:880): if AL = 0xFF on entry, find the `'D'` row whose path's final component matches the record's; AL = that zone; then join the existing VOL+path check, which will match. Zones renumber at boot because `ovl_sc_load` compacts rows, so a package cannot hold its zone across boots. Remove-by-name is what makes the close work.

Toasts: no new kernel string. A full table answers `Too many shortcuts`, the module's existing string.

**≈ +100-130 B of the settings-core image.** Measure `MODS_SIZE` before and after: 2,447 B is 5 sectors with ~113 B to the sixth. One more sector inside a cylinder run is ~0 ms (§18.91.1).

**Not changed, on purpose:**
- `wm.inc`, `ui.inc`, `dock.inc`, `dockmod.inc`;
- the CFG key table and the `drv_cfg` struct;
- `ovl_sc_load` (0 `.ovl` bytes);
- `mod.inc` (no new module id);
- `drv_tab`;
- `SC_VER`, which stays 2. An older kernel shows a DIA row as a plain shortcut icon, which is harmless.

**Resident ledger (estimate, to be measured in W1):**

| | `.text` | `.cold` | `.bss` | `.ovl` |
|---|---|---|---|---|
| kern_big / kern_emu | +6 | ≈ +104 | 0 | 0 |
| kern_small | +6 | ≈ +24 | 0 | 0 |

That spends 6 of the image rung's 200 B and ~104 of the cold rung's 462. No rung is expected to be crossed.

### 3.3 The framework: `apps/os88dia.inc`

The caller defines:

| symbol | meaning |
|---|---|
| `DIA_DATNAME` | `'DESKCAL.DAT'` |
| `DIA_CAP` | `'Calendar'`, the caption of the row and of a downgraded shortcut, ≤ 12 characters |
| `DIA_PAYLOAD` / `DIA_PAYLEN` / `DIA_PAYVER` | the application's payload |
| `DIA_ICON` | its header's 64-byte icon body, at +32 |
| `DIA_ONTICK` | its near proc, called once per mux tick |
| `DIA_ONCLOSE` | its near proc, called before the row is removed |

The include provides:

1. **`dia_load`** (entry proc, before `OSAPI_WM_CREATE`; SI = `ld_lname`, DI = the template):
   - Copy the 8.3 name.
   - **Singleton:** for each window slot 0..11, call `OSAPI_WM_OWNSEG`. If a segment other than our DS has the same header name at +16, toast `Desk Calendar is open` and answer CF = 1, which declines the launch (`[ld_said]` keeps the kernel quiet).
   - Bank with `OSAPI_FILE_HERE` (0x01C1: DX = cluster, BL = volume).
   - Walk root → `SYSTEM` → `APPDATA` with `OSAPI_FILE_FIND` (0x0283: +14 type, +16 cluster) and `OSAPI_FILE_GOTO` (0x01C7). Use GOTO, not GOTO_Q (§19.9); Cyclone's `cy_data_enter`, `cyclone.asm:7361-7520`, is the worked example.
   - `OSAPI_FILE_READ` (0x0109) into a paragraph-aligned buffer with 80 B of slack, then GOTO back.
   - Validate the magic, the version and the **exact size**:
     - **missing:** use the defaults;
     - **damaged:** set `dia_damaged` and run empty;
     - **valid:** copy the payload in and place the window (anchoring rule below).
   - **No APPDATA folder:** set `dia_nohome`. The state stays in memory; say nothing (§19.9).
2. **`dia_made`** (after WM_CREATE, BX = window):
   - Store the window.
   - Install `OSAPI_WM_ONTIMER` (0x0342) → `dia_tick` and `OSAPI_WM_ONCLOSE` (0x0362) → `dia_close`.
   - Arm `OSAPI_WM_TIMER` (0x033A) for 1 tick.
   - If that answers CF = 1 (kern_small), set `dia_worker`; the first W_PAINT hires the worker (item 6).
   - Do **not** call `OSAPI_WM_NOANIM`: the SDK says not for a main window (os88api.inc:649), and an open animation at boot is harmless.
3. **`dia_tick`** (timer callback: UI task, lock held, file slots legal, §13.9):
   - (a) **First call only**, when `dia_nohome` = 0: build the 128-byte record in bss and call `OSAPI_DESK_ITEM` AL = 1. On a refusal (no system disk, table full, or kern_small), just stay unregistered; CTRL.DRV's own toast is the only message. The record:

     | offset | content |
     |---|---|
     | +0 | BL from `FILE_HERE` |
     | +1 | 0xFF |
     | +2 | 1 |
     | +3 | `'D'` |
     | +4 | `OSAPI_FILE_PATH` (0x041F) minus its leading `\`, then `\`, then the 8.3 name: `SYSTEM\DIA\DESKCAL.O88`, 22 of 45 |
     | +49 | `DIA_CAP` |
     | +64 | `DIA_ICON` |

     A path over 44 characters means no registration.
   - (b) **Position poll:** read `[es:bx+W_X]` and `[es:bx+W_Y]`. If they differ from the saved pair **and** equal the previous poll's pair, the drag has settled: store them with `OSAPI_VIDEO`'s AX and BX, and set dirty.
   - (c) Call `DIA_ONTICK` (the app's minute logic).
   - (d) If dirty and `dia_nohome` = 0, run `dia_save`.
   - (e) Re-arm for **36 ticks** (~2 s).
4. **`dia_save`:**
   - Bank, walk, `OSAPI_FILE_WRITE` (0x0103) the header and payload as one call, GOTO back.
   - On CF = 1 (the disk is swapped), keep dirty and retry on the next tick, silently.
   - If `dia_damaged`, the first save first asks `os88ui_ask AYESNO 'Replace damaged calendar?'`. On No, it stays read-only for the session.
5. **`dia_close`** (W_ONCLOSE negotiator: close box and File ▸ Close; **not** Restart or power-off, `ui.inc:2814-2850`):
   - Call `DIA_ONCLOSE`, then `OSAPI_DESK_ITEM` AL = 0, AH = 0xFF, ES:SI = the record.
   - Then `dia_save`, then answer CF = 0.
   - Because Restart does not close windows, an open widget **stays registered across a restart**, which is what the user asked for.
   - A Task Manager kill also leaves the row, so the widget comes back next boot. This is documented.
6. **kern_small arm** (`dia_worker`), from the first W_PAINT, as the `taskmgr.asm:1068-1123` pattern does:
   - `OSAPI_TASK_SPAWN`, then `OS88_WORKER_RESTARTABLE`. The worker loops `OSAPI_TASK_SLEEP` 36 / `OSAPI_TASK_ALIVE` / `OSAPI_WM_WAKE`, and touches no file (§20.6 rule 7).
   - The `W_ONWAKE` handler (no lock) runs (b), takes `OSAPI_GFX_LOCK` around `DIA_ONTICK`, so the app sees the same environment as on kern_big, then runs (d).
   - The registration (a) is skipped, because `OSAPI_DESK_ITEM` is `stc` there.

**Placement on restore**, user-first's anchor rule, applied in `dia_load`:
- If the saved screen width and height equal `OSAPI_VIDEO` AX/BX (0x0127), use the saved x,y.
- Otherwise anchor to the nearer edge on each axis:
  - `x' = x` if `x + w/2 < oldW/2`, else `newW - (oldW - x)`;
  - the same for y against the dock line (`OSAPI_VIDEO` CX).
- `wm_fit` inside `wm_create` (wm.inc:1339 → :511) then clamps whatever is left onto the glass (§39.7). That is the graceful answer for a resolution change.

**The DIA header** (16 B, owned by `os88dia.inc`, at the front of the app's DAT):

| off | size | field |
|---|---|---|
| +0 | 2 | `'DA'` |
| +2 | 1 | header version = 1 |
| +3 | 1 | flags: bit 0 position valid |
| +4 | 2 | frame x (W_X) |
| +6 | 2 | frame y (W_Y) |
| +8 | 2 | screen width at save |
| +10 | 2 | screen height at save |
| +12 | 1 | payload version (`DIA_PAYVER`) |
| +13 | 1 | 0 |
| +14 | 2 | payload length; must equal `DIA_PAYLEN` and file size − 16 |

**Estimated size:** ~650 B of image (with the singleton check and the worker arm), ~200 B of bss (record 128 + state).

### 3.4 Persistence: two stores, each where the OS keeps that kind of state

| store | holds | written by | when |
|---|---|---|---|
| SYSTEM.CFG trailer (§26.8.6): a `'D'` row, KIND 1, zslot 0xFF | **which** DIAs are open and **where the program file is**, i.e. "launched from" | CTRL.DRV settings core, via `OSAPI_DESK_ITEM` | first launch (later launches are a no-op compare, no write) and close |
| `SYSTEM\APPDATA\DESKCAL.DAT` on the widget's **own** volume (§19.9) | position, the screen it was saved on, appointments, tasks | the package, UI task only | debounced 2 s after an edit, once a drag settles (~4 s), and at close |

- Read at boot by `ovl_sc_load` (the boot overlay, no resident byte), which copies +3 verbatim.
- Launched by `dia_boot_x` → `sc_open` → `ldf_ld_pkg_start` → `dia_load`.
- Rows are shared with the user's shortcuts: `SC_MAX` = 15 rows, `ovl_sc_load`'s 4,096-byte cap (desksc.inc:1203). A DIA row costs **no** desktop cell.
- kern_small reads no SYSTEM.CFG (§51.5), so restore does not exist there. The DAT still restores position and data at launch.

**Graceful failure table:**

| case | what the user sees | state |
|---|---|---|
| widget file deleted or renamed, `SYSTEM\DIA` gone, or a file where a folder was | one toast `Shortcut not found (A:)` (the existing sentence, `sc_s_nf`, drive patched in); a **Calendar** icon appears in a desktop cell | the row is downgraded in memory to an ordinary shortcut. The user can double-click it (retry) or Delete it. The next SYSTEM.CFG write persists it |
| launched from B:, booted without that disk | the same, naming `(B:)` | the same, and stated in §107 |
| load refused (memory, version) | `ld_say_status`'s sentence | the row is **kept** |
| DAT missing | a fresh calendar at the template position | — |
| DAT damaged | an empty calendar; the first edit asks `Replace damaged calendar?` | the file is never silently overwritten |
| saved position off this screen | the window lands anchored and clamped | — |
| hibernate or DOS question pending | nothing is launched | a Resume brings the session back. A Discard leaves the widgets for the next boot (W5 option D4) |
| N misses | one toast (single slot, last wins, §59.3); every miss has its own visible icon | — |

Boot always continues to the desktop.

### 3.5 Disk layout and Makefile

```
A:\SYSTEM\DIA\DESKCAL.O88        LZ4-packed by PKGZ, ~4-5 KB, ~5 clusters at 360 KB
A:\SYSTEM\APPDATA\DESKCAL.DAT    written at runtime, never shipped (so never packed)
```

- `DIAAPPS := $(BUILD)/deskcal.o88` and `DIAARGS := $(addprefix SYSTEM/DIA:,$(DIAAPPS))`.
- Thread it **explicitly**, as zero-kernel says, rather than through `SYSAPPSARGS`. That variable is restated in ~12 tight 360 KB gate-disk recipes; the uses are listed at `Makefile:4266-4690` and 9935-11744.

| recipe | location |
|---|---|
| `$(IMG)` | :4135 |
| `$(IMG720)` | :4150 |
| `$(IMG120)` | :4177 |
| `$(IMG360)` | :4643 |
| `$(BUILD)/emu.img` | :11347 |
| `LIVESYSARGS` (live media and `usb-emu`) | :13005 |
| small system disks, via a new `SMALLDIAARGS` | :11272/:11287 |

- Add the prerequisite `$(DIAAPPS)` to each of those recipes.
- `$(BUILD)/deskcal.bin` and `.o88` rules in calc's shape (`Makefile:5206-5212`).
- Not on any apps disk. A widget belongs to the boot volume (the THEWIRE rule, §92).
- 360 KB system disk: **42 free clusters today** [measured by map-cfg]; this spends ~6 (1 directory + the packed package), plus 3 at runtime for the DAT.
- APPDATA capacity is ~14 entries at 1.44 MB/1.2 MB (FERR_DIRFULL, §18.5). DESKCAL uses one; each future DIA should keep to one file.

---

## 4. The Desk Calendar (`apps/deskcal/`, §107.3)

- **Files:** `deskcal.asm` (header, entry, menus, callbacks), `dcdraw.inc`, `dcdate.inc`, `dcdata.inc`, `dcrem.inc`.
- **Header:** `OS88_HEADER 'Desk Calendar', dc_entry, OS88_F_ICON`.
- **Includes, at the end before `OS88_BSS`:** `os88ui.inc` with `OS88UI_CHK`, `OS88UI_ALERT` and `OS88UI_ABOUT`, then `os88line.inc`, then `os88dia.inc` (os88line must follow os88ui, `os88line.inc:18-22`).
- **About:** `OSAPI_ABOUT_SET` is mandatory (§12.2; the SHEET/CHART lesson).
- **Estimate:** image ≈ 8-9 KB (the shared UI includes are 2,798 B measured by map-pkg; dia ~0.65 KB; the calendar ~4.5 KB); bss ≈ 2.9 KB; ~4.5 KB on disk.

**Window setup:**
- non-sizable; snap on (the default);
- `OSAPI_WM_KEEPH` (0x02F0);
- `OSAPI_WM_OWNBG` (0x02C5): the painter lays every pixel of ground;
- `OSAPI_WM_SAVEU` (0x02A4) with AL = 3, i.e. ON | **1BPP** (§11.96.17), because the face is two-colour on **every** adapter (ink 0, ground 15). The save-under is 1/4 size and an uncover is a blit;
- `OSAPI_WM_PREFER` (0x036E) gets the per-adapter frames below;
- `OSAPI_WM_ONRESIZE` (0x02EB) re-derives the layout after §39.11's adapter switch.

**Date maths** (copied in, ~80 B):
- Sakamoto's day of the week from `dos_dow` (`apps/dos/dos.asm:13748`, table at :10995), exact over 1980-2099;
- days in a month from mask `0x15AA` plus `year AND 3` (`clk_mlen`, `clock.inc:1802`);
- three-letter month names in `clk_mnames`' shape;
- navigation clamped to 1980-01 .. 2099-12, with the arrow greyed at the end.

### 4.1 Layout

Every text pen sits at content origin + 8k. Every row is **one opaque `OSAPI_FONT_RUN`** (§6.1), with no fill followed by glyphs. State is never carried by colour.

**TALL** (`OSAPI_VIDEO` BX ≥ 300: VGA 640×480, EGA 640×350, Hercules 720×348). Content 208 × 236; frame ≈ 210 × 255.

| content y | row |
|---|---|
| 0 | header `<     October 2026     >`, 26 cells, inverse; cells 0 and 25 are the step arrows |
| 12 | weekday row `Su Mo Tu We Th Fr Sa`, 21 cells from x+16 |
| 24-83 | six week rows at a 10-px pitch, one run each. A day is 3 cells, `dd*`, where `*` means it has items. **Today** is inverse (that row is split into three runs); the **selected** day gets one `OSAPI_GFX_FRAME` |
| 88 | rule (`gfx_hline`) |
| 92 | day heading `Tue Oct 13` + `New: ` hint |
| 104-153 | 5 agenda lines at a 10-px pitch: `HH:MM! text` (time 5, mark 1, space, 18 text cells), or ` --  ` for all-day. `!` = reminder armed, `-` = fired |
| 158 | `Tasks  3 open` |
| 170-229 | 5 task rows at a 12-px pitch: an `os88ui_chk` 12-byte record (box in cells 0-1) plus 21 text cells |
| bottom | an `os88line` field (20-byte block), shown only while editing, in place of the last row of the active pane |

**COMPACT** (CGA 640×200, 155-row desktop). Content 208 × 120; frame ≈ 210 × 139.
- Header and weekday rows at an 8-px pitch.
- Six week rows at an 8-px pitch.
- A tab row ` Day   Tasks `, with the active tab inverse.
- One 4-row pane showing either the agenda or the tasks.
- The pane code is the same, parameterised by row count and pitch, so the two layouts are one code path with two tables.

### 4.2 Data: `DESKCAL.DAT` payload v1

The file is the 16-byte DIA header (§3.3) followed by this payload. Fixed size **2,456 B = 5 sectors**, one read call and one write call.

```
+0     db n_appt (0..48), n_task (0..32), lead_default (0/5/15/30/60), flags
+4     dw 0, 0
+8     48 x 32 B APPT, kept sorted by (date, time) on insert:
         +0 dw date   FAT-packed ((y-1980)<<9 | m<<5 | d), a word compare orders it
         +2 dw time   minute of day 0..1439, 0FFFFh = all day
         +4 db flags  bit0 REMIND, bit1 FIRED
         +5 db lead   minutes before
         +6 26 B text, NUL
+1544  32 x 28 B TASK:
         +0 db flags  bit0 DONE, bit1 DATED
         +1 db 0
         +2 dw due    packed date, valid iff DATED
         +4 24 B text, NUL
= 2,440 B
```

- Every limit is a refusal, never a truncation. The 49th appointment greys `New Appointment` with the reason in its item text (§47, "grey a fact").
- `FIRED` is cleared whenever an appointment's date, time or REMIND changes.

### 4.3 Input

- **One-line entry**, from risk-first. `N` (or Calendar ▸ New Appointment) opens the `os88line` field on the selected day.
  - `9:30 Dentist`, `930 Dentist` and `0930 Dentist` all parse to a timed appointment, created with REMIND on and the default lead.
  - Text with no leading time is an all-day item.
  - Enter commits (`os88line_key` hands back CF = 1), Esc cancels.
  - `K` / New Task does the same in the task pane.
  - On a selected line, Enter re-opens it in the field.
- **Clicks:**
  - a day selects it; `<` / `>` step the month;
  - an agenda line selects it, and clicking its `!` column toggles REMIND;
  - a task's box toggles DONE via `os88ui_chkhit` (os88ui.inc:3487), which redraws only the mark.
- **Keys** (when frontmost; keys go to `wm_top`, `ui.inc:191-200`): arrows move by a day or a week, PgUp/PgDn change the month, `T` goes to today, Del deletes the selected item after `os88ui_ask AYESNO`.
- **Menus** (`OS88_MENUSET`, ≤ 5 menus, ≤ 24 glyphs):
  - **Calendar:** New Appointment, New Task, Go to Today, Clear Done Tasks, Close;
  - **Remind:** At Time, 5 Min Early, 15 Min Early, 1 Hour Early. These set the lead of the selected appointment and the default.

  The About card is `os88ui_about` with 4 lines.

### 4.4 Reminders (`DIA_ONTICK`, every 36 ticks)

`OSAPI_CLK_GET` is called. When the minute differs from the last one seen:
1. **Date rolled:** move today's inverse (two 3-cell runs, ~8 ms), plus the header if the month changed.
2. **Due check:** find the first appointment with REMIND set, FIRED clear, date = today, a time, and `time − lead ≤ now`. If no alert is up:
   - set FIRED and dirty;
   - `OSAPI_SND_TONE` (0x00D3): 880 Hz then 660 Hz, 4 ticks each, DL = 0x40;
   - `OSAPI_TOAST` (0x02AA) with `09:30 Dentist`, the user's text last, because only 24 characters reach the bar (§59.10);
   - `os88ui_ask OS88UI_AOK` with `Reminder 09:30 Dentist`, ≤ 34 characters. Its completion proc clears `dc_alert`;
   - redraw that agenda line (`!` becomes `-`).

   Further due items wait one minute tick each, so alerts never stack.

**At launch:** today's due-but-unfired reminders are marked FIRED and summarised in one toast, `2 missed reminders`. Earlier days are not replayed.

**No RTC** (`CLK_GET` AL bit 0 = 0): the first launch toasts `Clock not set (no RTC)`.

Reminders fire only while the widget runs, including while minimized (§13.9). Autostart is what makes that the normal case; §107 and readme.txt say so.

### 4.5 Redraw budget (5150, PERFORMANCE.md's table; to be confirmed on MartyPC)

| event | cost |
|---|---|
| full paint | ≈ 22 runs + ~8 other calls ≈ 0.45 s; only at open, and after an uncover the save-under cannot serve |
| select a day | 2 day fields + the agenda pane, ≈ 9 runs ≈ 0.2 s |
| toggle a checkbox | the mark only, 4.3-6.4 ms |
| idle tick | **0 draw calls** |
| minute with nothing due | 0 draw calls |

---

## 5. kern_small

- Ship `DESKCAL.O88` in `SYSTEM\DIA\` on the small system disks too (`SMALLDIAARGS`). It needs no driver and ~12 KB of heap, so none of §24.5's three grounds (driver unreachable, claim larger than the machine, no reader) applies.
- What works there: the widget, its data and its position (from the DAT at launch). Ticks come from the worker arm (§3.3 item 6).
- What does not: autostart and restore. `OSAPI_DESK_ITEM` is `stc` (`desk.inc:2213`) and is refused silently; `OS88_SHORTCUTS` and `drv_notice_x` do not exist there.
- The inline editor rather than a second window is what keeps it inside kern_small's 6 window slots.
- `soak -k smallreq` reads it off the built floppy.
- Kernel cost: K1 only (+6 `.text`, ≈ +24 `.cold`). Measure with `tools/kernsize.py --build build/smallk` before and after (not measured in this pass). This is open decision D3.

---

## 6. SPEC.md work (wave 0, written before any code)

- **New §107 "Desktop Integrated Applications"**:

  | subsection | subject |
  |---|---|
  | §107.1 | definition: package, `SYSTEM\DIA`, `os88dia.inc` |
  | §107.2 | the DIA row (`'D'` at +3, KIND 1, zslot 0xFF), add-or-update and remove-by-name |
  | §107.3 | the Desk Calendar |
  | §107.4 | the boot relaunch (`dia_boot_x`) and the failure table |
  | §107.5 | the DIA header and the DAT payload |
  | §107.6 | what it costs (the kernsize ledger) |
  | §107.7 | kern_small |
  | §107.8 | what is not done: desk layer, chrome, a move notification, a shutdown hook |

- **Amend:**
  - §26.8.1: the +3 byte's three real values. This fixes existing drift: +3 is `\` today, not spare.
  - §26.8.5: `sc_open`'s CF.
  - §26.9 / §26.9.3: `desk_live` and DIA rows.
  - §26.9.6: the `OSAPI_DESK_ITEM` DIA record and remove AH = 0xFF.
  - §2.8.7: the settings core's DIA rules.
  - §20.3: cell 0x0467 and the table-length line.
  - §37: `OSAPI_CLK_GET` as a reader.
  - §51.3: `dia_boot_x` at the head of `drv_notice`.
  - §19.9: `DESKCAL.DAT`.
  - §24.5.1: the small system disk carries `SYSTEM\DIA`.
- Also: docs/KERNEL-MEMORY.md (ledger), PERFORMANCE.md Part 5 (a row in W4), docs/INDEX.md (regenerated), and a root `readme.txt` section (`tools/checkreadme.py`).
- **Renumber check before merge:** another branch could take §107 first (the port-workflow lesson).

---

## 7. Waves (each builds, gates and ships on its own)

**W0 — SPEC first.** §6 above. Gate: `make` (checkdocs).

**W1 — kernel and module half, no calendar yet.**
- K1-K6; SDK defines and contract text; `abilock --update` with no bump; the `clk_hour − clk_min` assertion.
- `tests/diaprobe/diaprobe.asm`, built by `make bench` only and never shipped (deskitem's shape): it includes a minimal `os88dia.inc` arm (register on its first tick, remove by name on close) and paints its own x,y.
- `tools/os88dia.py` (with `--selfcheck`): an independent host writer and reader for the SYSTEM.CFG trailer (rows, pin bytes, footer, the `'D'` row) and for the DAT (DIA header + DESKCAL payload).
- `make diatest`: scratch 1.44 MB and 360 KB system images (ethertest's shape) with fixture SYSTEM.CFG and DAT files.

  | variant | contents |
  |---|---|
  | `restore` | a DIA row → DIAPROBE at (304,184) |
  | `ghost` | a row → `SYSTEM\DIA\GONE.O88` plus the restore row |
  | `bvol` | a row on B:, booted with no B: |
  | `short` | three ordinary drag-made shortcuts with `\` at +3, and no DIA row |
  | `hiber` | a hibernate image present |

- Checks: `tools/kernsize.py` before and after on both kernels, `--bless`, docs/KERNEL-MEMORY.md; `os88sym --all` on both kernels and `os88ladder --selfcheck` (memory: instruments break silently); verify that `desk_reflow` is lock-free-safe and that `OSAPI_FILE_HERE`'s BL equals the DI record's volume index.
- Gate rows: `diaboot`, `diamiss`, `diashort`, `diahiber`; `desksc`, `deskitem` and `cfgtrip` stay green; `make test-full` run alone.

**W2 — the framework and the calendar skeleton.**
- `apps/os88dia.inc` in full: singleton, anchoring, register, remove, mux, save, worker arm.
- `apps/deskcal/`: window, About, menus, grid, navigation, date maths, the TALL and COMPACT tables, ONRESIZE.
- Makefile: rules, `DIAARGS` threaded through all seven recipes of §3.5.
- `t_dia`, `t_livefull`, `t_smallreq` green; INDEX regenerated.
- Gate rows: `diaregister`, `diamove`, `deskcalgrid`, plus the first VGA and CGA screenshots.

**W3 — appointments and tasks.**
- One-line entry and its parser, sorted insert, agenda select / edit / delete, tasks with `os88ui_chk`, Clear Done.
- DAT payload v1 load and save; damaged-file confirm; capacity greying.
- Gate rows: `deskcaledit`, `deskcaltask`.

**W4 — reminders, kern_small, measurement, PR.**
- Minute pass, tone, toast and alert, the missed-today summary, midnight rollover, the no-RTC hint.
- The kern_small worker arm end to end; small system disks.
- MartyPC counters and the PERFORMANCE.md Part 5 row; readme.txt.
- Full screenshot set; functional-check skill pass; PR.
- Gate rows: `deskcalremind`, `diasmall`, `deskcalperf`.

**W5 — OWNER DECISION D1, not built by default: the desk layer.** The wm map §8 items a-g, ≈ 100 B resident, mostly hot-path `.text`, so it cannot be a module:
- `WF_DESK` = W_FLAGS bit 10, added to `WF_HIBITS` (wm.inc:286);
- a new cell `OSAPI_WM_DESK` (0x046D; its kern_small body refuses);
- `wm_lift` keeps desk windows at the top of the bottom group;
- `wm_show_b` / `wm_front_b` paint through `wm_paint_dmg`, not `wm_raise`;
- the content press skips the raise (`ui.inc:457`);
- `IF_NOTILE` (I_FLAGS bit 2) in `db_key` / `db_hit` plus DOCK.DRV;
- `wm_dc_ok` skips the drag cache for a covered desk window.

Chrome removal (user-first's ten `WF_FULL` sites, +60-100 B) is a **separate** later decision. Budget check: the image rung has 200 B left.

---

## 8. Tests (`tests/suite.py` rows)

**FAST** (host, inside every `make`):
- **`t_dia`** (`tests/unit/t_dia.py`):
  - Every shipped **system** image carries `SYSTEM/DIA/DESKCAL.O88`, read by the independent FAT12 reader. The images: four geometries, four small, `emu.img`, and `os8088-usb.img` when built.
  - **No** apps image carries it.
  - `tools/os88dia.py --selfcheck` round-trips: exact-size, magic and count refusals, and the `'D'` row.
  - A constant mirror holds `SC_R_DIA` = `OSAPI_DI_DIA` = 3 and `SC_DIA` = `OSAPI_DIA_MARK` = `'D'`, plus the DIA-header and payload offsets across `os88dia.inc`, `deskcal.asm` and `os88dia.py`.
- Existing rows that must go green: `t_api_abi` and the abilock row (cell 0x0467), `t_mirror` (new pairs), `t_livefull` part A (`DESKCAL.O88` in LIVEARGS through `LIVESYSARGS`), `t_image`, `t_diskverify`, `t_registry`, the INDEX `--check`, `checkreadme`.

**FULL:** no new row; this keeps the ~50 s budget. `t_buildmatrix` (kern_small, kern_emu, knob kernels) and the 1bpp desktop boots cover assembly.

**SOAK** — group `dia*` / `deskcal*`. On MartyPC per docs/TESTING.md and the `deskitem` precedent (`needs=("marty",)`) unless noted:

| row | what it asserts |
|---|---|
| `diaboot` | `diatest restore`: DIAPROBE's window is at (304,184) after the snap; no desktop cell taken (`desk_zslot` of its zone is 0xFF); desktop reached; SYSTEM.CFG unchanged (no write) |
| `diamiss` | `ghost` and `bvol`: the desktop is reached; the toast text is `Shortcut not found (A:)` / `(B:)`; a Calendar icon occupies a cell; the surviving widget launched |
| `diashort` | **the regression for §2.1**: `short` boots, all three drag-made shortcuts are drawn in their cells, nothing is launched |
| `diahiber` | with a hibernate image: no package instance exists when the Resume question is up |
| `diaregister` | open A: ▸ SYSTEM ▸ DIA and double-click DESKCAL.O88 (one process, per the double-click-latency lesson); read the image back: one `'D'` row with path `SYSTEM\DIA\DESKCAL.O88`, KIND 1; relaunch: still one row, SYSTEM.CFG byte-identical; close box: the row is gone; reset: no widget |
| `diamove` | drag the title bar; wait 6 s; DAT read-back holds the new x,y; reset (wait on a kernel symbol, not `settle()`, per the warm-boot lesson); the window is back at the same place |
| `diasmall` | `make small`: the widget opens, `OSAPI_DESK_ITEM` refuses silently (no toast), reset brings back no widget, a relaunch restores position and data |
| `deskcalgrid` | read the package's 42-byte day grid out of its bss (via `I_SPTR`) for Oct 2026, Feb 2028, Jan 1980 and Dec 2099 and compare with Python's `calendar.monthcalendar`; stepping past 2099 is refused |
| `deskcaledit` | type `9:30 Dentist` Enter (sendkey); the DAT holds date, 570, REMIND; Del + Yes removes it; the 49th add is greyed |
| `deskcaltask` | add `Buy milk`, click its box: the mark pixels flip and DONE is set in the DAT after the debounce; Clear Done compacts |
| `deskcalremind` | poke the live `clk_*` to 09:29:40 (tests/kdreturn.py:387's reader, inverted); the alert window is `wm_top` with `Reminder 09:30 Dentist`; the toast buffer holds `09:30 Dentist`. Plus a QEMU `make test-snd` arm with `tools/sndcheck.py` for 880 Hz |
| `deskcalperf` | counters in `font_run` / `gfx_fill` / `wm_paint_dmg`: first paint, day select, an idle tick (must be **0**), a reminder tick, and boot time with and without a DIA row (`tools/os88boot.py` phase table). These figures go into PERFORMANCE.md Part 5 and §107.6 |
| `deskcal1bpp` | CGA (MartyPC `os8088_5150_cga`) and Hercules (`os8088_5150_herc_gla`): the COMPACT and TALL layouts, today inverse, the selected frame, greyed items legible (§39.4, §47) |

Hazards (memory notes): run soak groups one at a time with `--marty-jobs 1`; a MartyPC client is single; knob kernels go in private trees; never run test-full beside another make.

---

## 9. PR screenshots (the user's request)

**Fixture.** `make diashot` builds `build/diashot.img`: the system disk plus a SYSTEM.CFG trailer with the DESKCAL `'D'` row and a `DESKCAL.DAT` written by `tools/os88dia.py`. The DAT is placed at the desktop's right, with three appointments today (one with a reminder at **host-time + 2 min**, generated at build time) and four tasks (one done). QEMU's RTC is the host clock, so the grid shows the real month.

**VGA:**
```sh
make diashot && make test TESTIMG=build/diashot.img       # widget autostarts at boot
python3 tools/shot.py build/qmp.sock docs/dia/full-vga.png               # full frame FIRST
python3 tools/shot.py build/qmp.sock docs/dia/vga.png --crop X,Y,W,H --zoom 2
```

**One 1bpp adapter, CGA.** QEMU's VGA renders CGA mode natively, so `shot.py` works. Build it in a private tree so `build/` keeps the stock kernel:
```sh
make BUILD=build-cga VIDEO=cga build-cga/diashot.img
make BUILD=build-cga VIDEO=cga test TESTIMG=build-cga/diashot.img   # QMP is still build/qmp.sock
python3 tools/mouse.py --screen 640x200 build/qmp.sock ...
python3 tools/shot.py build/qmp.sock docs/dia/cga-compact.png --crop ... --zoom 2
```
Hercules comes off MartyPC (`os88marty.py ... shot herc.png`, docs/HERCULES-TESTING.md) as a bonus.

**The set, one per user-visible claim, committed under `docs/dia/`** (docs/*.png are tracked precedent; never anything in `build/`):

| file | claim |
|---|---|
| `full-vga.png`, `vga.png` | widget on the desktop: month, today inverse, agenda, tasks |
| `vga-moved.png` → `vga-restored.png` | dragged somewhere, then `system_reset`: the same place |
| `vga-reminder.png` | alert plus the menu-bar toast |
| `vga-task-checked.png` | a checkbox toggled |
| `vga-editing.png` | the one-line entry |
| `cga-compact.png` | 1bpp COMPACT layout, legible greys |
| `vga-notfound.png` | DESKCAL renamed on the host image: the toast plus the downgraded Calendar icon |
| `herc.png` | optional, MartyPC |

**PR body:**
- each image embedded as `![claim](https://github.com/jggonz/os8088/blob/<head-sha>/docs/dia/<f>.png?raw=true)`, pinned to the head SHA;
- the functional-check skill's `claim | driven | glass | verdict` table;
- the kernsize ledger for both kernels;
- the compromise list (§10);
- the W5 decision.

---

## 10. Stated compromises and risks

1. **The v1 widget is a normal window.**
   - It opens on top and is raised by a click, which swallows the first click on a covered widget (`ui.inc:456-462`).
   - It has a title bar, frame and shadow, drags by its title bar only, takes the menu bar while in front, and has a dock tile.
   - W5 / D1 is the costed fix.
2. **No move notification and no shutdown hook.** Position is polled every 2 s and written once two polls agree. A power-off within ~4 s of a drag, or ~2 s of an edit, loses that change.
3. **Floppy cost.**
   - Each autostarted widget adds roughly 8-10 `int 13h` calls ≈ 3-4 s to an XT floppy boot: path walk, package read, APPDATA walk, DAT read, the first-tick settings-core read. A machine with no DIA row pays nothing.
   - Each committed edit or settled move is one DAT write (~1-2 s, pointer tracking per §7.4).
   - First registration and close each write SYSTEM.CFG once.
4. **The boot launch runs in kmain's tail**, before ui_task's first pass. No package has been loaded from there before (the Control Panel is a builtin). `diaboot` gates it. If the loader misbehaves there, the fallback is a `[ui_post]`-gated post at `ui.inc:597-600`, about 10 B of `.text`.
5. **Shared rows.** A DIA row is one of `SC_MAX` = 15 shortcut rows. A full table means the widget runs but is not restored, and `Too many shortcuts` is the only notice.
6. **A hostile SYSTEM.CFG** can mark any package row `'D'` and have it autostart. Accepted: SYSTEM.CFG is hidden+system on the boot disk, and whoever can write it can write KERNEL.SYS.
7. **ABI fence.** `DESKCAL` calls 0x0467, so on an older kernel at the same PKG_FMT it would far-call past the table. It ships only beside its own kernel and must **not** go into `wire.json` (D5).
8. **HDD install.** A row's VOL is a volume index. After an install, a row recorded against A: misses and is downgraded; that is existing shortcut semantics, stated in §107.
9. **Rungs.** +6 / ≈ +104 bytes against 200 / 462 left today. A concurrent PR can cross first, so re-run kernsize at merge on both kernels.
10. **Settings core** may grow from 5 to 6 sectors (`MODS_SIZE` 2,447 + ~120). Measure it.

---

## 11. Open decisions for the user (with recommended defaults)

| # | decision | recommended default |
|---|---|---|
| D1 | Desk layer (behind other windows, one-click, no dock tile), ≈ 100 B resident hot path; chrome removal is +60-100 B more | **Defer.** Ship v1 as a normal window, look at the screenshots, then decide W5. |
| D2 | A widget that cannot be found at boot | **Downgrade to a visible shortcut + one toast** (the user can retry or Delete), rather than silently retrying every boot. |
| D3 | kern_small | **Ship it on the small system disks**: widget + data + position, no autostart. |
| D4 | Relaunch widgets after a hibernate **Discard** (`CMD_DIA` post: ≈ 8 B `.text` + ~5 B HIBER.DRV) | **Defer**: they return on the next boot. |
| D5 | Offer DESKCAL on The Wire | **No**, until a "highest cell used" fence exists. |


---

## Where the code proved the design wrong - the builders' log

Numbered as the waves appended them; SPEC.md §107.9 carries the same in
prose.


## Wave 1 (kernel half, SDK, os88dia.py, diaprobe, diatest, four soak rows)

1. **K1 assertion**: `clk_hour - clk_min == 1` was NOT added as a new %if - the
   first %if beside the fields already asserts `clk_min - clk_sec == 1` and
   `clk_hour - clk_sec == 2`, the same fact. Its %error message now names
   `osapi_clk_get_x`'s two word reads instead.
2. **K1 masks clk_h12** to bit 0 (+2 B): CFG_H12 comes from SYSTEM.CFG, a file's byte.
3. **K4 needed a third site**: `sc_open`'s folder arm ended `jmp fm_choose`
   (flags undefined) -> `call fm_choose / clc / ret` (+2 B), so "CF = 1 iff not
   found" holds for every arm.
4. **K5 is near**: `dia_boot` (ret) called `call dia_boot` from `drv_notice_x`;
   both are .cold so the design's far `dia_boot_x`/`call COLD_SEG:` was 2 B of
   call for nothing. A future far caller (W5/D4 CMD_DIA) needs a 4-byte thunk.
   It preserves ALL registers (kentc_bp + push es / jmp kretc_es) because
   drv_notice_x's contract says so; KIND and mark are ONE word compare.
5. **K6 order**: a DIA record's KIND==1 and VOL<DVOL_MAX are checked BEFORE the
   by-name find, because the update-in-place writes over a live row and the
   existing post-copy `.bad` path frees the row it wrote into.
6. **K6 size**: MODS_SIZE grew 2,492 -> 2,673 (+181), 5 -> 6 sectors. Design
   estimated +100-130 from 2,447 (which had already grown to 2,492).
7. **Fixture place (303,184)** not (304,184): WF_SNAP keeps W_X = 7 mod 8 (SPEC 11.94).
8. **apps/os88dia.inc shipped in W1** as the subset the probe needs (state file
   load/validate/place, register on first tick, 36-tick poll + save, remove
   by name + save on close). W2 adds: singleton, anchoring to another screen
   size, damaged-file ask, kern_small worker arm. Layout contract: DIA_BSS
   multiple of 16 and the image ends on a paragraph (`align 16` before
   OS88_IMAGE_END); dia_rec doubles as the read's 80-byte slack and the walk's
   find buffer; payload is DIA_PAY (= dia_file + 16), owned by the include.
   The include also skips registration when the DAT was DAMAGED (design: only nohome).
9. **DIAPROBE is built by `make diatest`** (deskitem's precedent), not `make bench`.
10. **diahiber uses a hard disk**: hb_pick only asks a FIXED volume, so the
    fixture is a per-process VHD (os88hdd.py --noboot --file HIBERNAT.PTR=junk)
    plus diahiber360.img wanting HDD.DRV; asserted by a breakpoint on dia_boot
    reading [ui_rebootq] == UI_RBQ_ASK, then 20 guest s with no DiaProbe.
11. Verified, not changed: desk_reflow is lock-free-safe (no deferred path);
    OSAPI_FILE_HERE's BL is inst_fdrv = the row's volume index (diaboot's
    byte-identical SYSTEM.CFG measures it).
12. Mirror: SC_MAX added to tools/os88geom.py's _MIRROR (t_mirror refused two
    host copies - tests/desksc.py and tools/os88dia.py - without an entry).

## Wave 1 fixes (after review; SPEC.md 107.5, 107.5.1, 107.6, 107.9 carry the same)

13. **The save's walk is ONE full OSAPI_FILE_GOTO (the root) then
    OSAPI_FILE_GOTO_QM per step**, not a full GOTO per component (design 3.3
    and SPEC 19.9). GOTO_QM moves the instance, which is what 19.9 refused
    GOTO_Q for lacking; the first stays full because dsk_bpb_sig cannot tell
    two os8088 disks apart and only a full mount re-reads the FAT. Measured:
    a settled drag's save 6R+6W -> 2R+6W. SPEC 19.9 gains a note.
14. **A save checks HOME first**: the banked launch folder must hold a file
    of the package's own name, else "not home now" (save stays owed). The
    design trusted 19.9's "in the drive by definition"; measured, an apps
    disk in A: got SYSTEM\APPDATA\DIAPROBE.DAT. HOME is banked ONCE, at
    dia_load, not at every save.
15. **Failed saves are classified**: FERR_NODISK/FERR_IO/not-home retry on a
    1,2,4,8,16-poll backoff; anything else (WPROT, FULL, DIRFULL, PROT) sets
    DIA_S_RDONLY for the session. Design said "keep dirty, retry every tick".
16. **Registration on every launch regardless of the DAT** (DAMAGED and
    NOHOME too). Design 3.3(a) skipped it with no APPDATA, W1 also when
    DAMAGED; either left a row from an earlier session unremovable.
17. **dia_fnd**: the walk's find buffer is its own 24 bytes (bss +26 with the
    two backoff bytes); dia_rec stays the row record to the close. The W1
    blocker.
18. tools/os88dia.py's CFG_FBUF is 141 after merging main (#248), held by
    the new fast row t_dia (nasm evaluates the kernel's own equ chain;
    os88geom refuses expressions so t_mirror could not).

## Wave 2 (os88dia.inc in full, apps/deskcal skeleton, the disks; SPEC.md 107.3, 107.5.2, 107.6, 107.9 carry the same)

19. **Layout chosen from the CONTENT HEIGHT** (>= 236 TALL, else COMPACT), not
    OSAPI_VIDEO BX >= 300: same answer on one display, right after a drag
    onto another display (only the box knows).
20. **Day field is [mark][d][d] with glyph 7**, not "dd*": '*' fills all 8
    columns in its middle row, so a frame on the field's outer column would
    cut it. With the mark first and a glyph whose edge columns are clear,
    the selection frame and its white erase never touch a glyph pixel.
    COMPACT week pitch 9 (not 8) so the frame's top/bottom stand in a gap row.
    Layout y tables: TALL 0/12/24/10/88/92/104, COMPACT 0/9/18/9/72/75/84.
21. **Menus carry only what works**: Calendar = Go to Today, Previous Month,
    Next Month. The design's Calendar > Close is the app-name menu's kernel
    Close already. W3/W4 add the rest.
22. **AM_NAME must be non-zero** - a KERNEL DEFECT found, not fixed (W2
    changes no kernel byte): menu_relayout's instance-name fallback jumps to
    .kseg, which sets [menu_seg] = 0 (KERNEL_SEG) for the name AND every
    menu cell, so a package passing AM_NAME = 0 (which the SDK allows) gets
    its menu titles/items read out of kernel memory. No shipped package
    passes 0. Calendar names its set. Owner decision: fix menu.inc (set
    [menu_seg] for the name alone) or reword the SDK comment.
23. **Singleton toasts and refuses; it does not raise the open window**: a
    slot maps to an owner SEGMENT (OWNSEG), not to another package's window
    pointer (which WM_FRONT needs; the stride differs per kernel).
24. **Anchor y uses the saved screen HEIGHT** (the header has no dock line);
    wm_fit clamps above the dock. x and y use the TEMPLATE's w/h.
25. **kern_small wake takes the lock around the whole beat** (poll, ONTICK,
    save) via one shared dia_beat, not only around DIA_ONTICK: the save runs
    in exactly kern_big's timer environment. Registration skipped on the
    worker arm.
26. **Damaged-file question is optional** (DIA_DAMAGEDQ + OS88UI_ALERT), never
    asked from the close (the alert would die with the package), No /
    dismiss = DIA_S_RDONLY for the session.
27. **App state first in bss** (DIA_BSS = 112): gates read the grid at
    image + 0; tools/os88dia.py names the offsets (DESKCAL_BSS_*,
    dia_st_off) and t_dia has NASM evaluate deskcal.asm's own equs.
28. **t_dia reads the small pair and emu.img only when built after the
    package** (their targets are on demand; a stale image would make `make`
    red on something nothing rebuilt). The live media is t_livefull part A's.
29. **tools/os88index.py counts SYSTEM/ and SYSTEM/DIA/ packages as shipped**
    (The Wire flipped no -> yes too; it was misreported before).
30. Today from OSAPI_CLK_GET is clamped to 1980-01-01..2099-12-31, and a
    clock field out of range (month 0, day past the month) reads as
    1980-01-01.

## Wave 2 review fixes (SPEC.md 107.3, 107.5, 107.5.2, 107.9 carry the same)

31. **wm_fit inside wm_create does not "clamp whatever is left" usefully**
    (design 3.3's placement rule): it runs BEFORE the entry proc's PREFER and
    KEEPH, which resize without moving (SPEC 11.100.9), so on CGA it clamped
    the restored y against the 255-row TALL template (y -> 20, always). And
    wm_fit keeps every frame that FITS the band above the dock, KEEPH or not
    (the flag only lowers the floor for a frame taller than the band), while
    wm_dock_snap lets a drag leave one more than half over the strip. Fix:
    deskcal sizes dc_tpl from its own PREFER row for OSAPI_VIDEO's kind
    before dia_load/create (EGA -> VGA's row), and dia_made writes a
    pulled-UP restored y back into the (not yet shown) record, down to
    OSAPI_WM_DISPLAY height - W_H - 1 (wm_fit's own KEEPH floor). First try
    (re-fit via OSAPI_WM_KEEPH with the flag as it stood) was MEASURED not to
    work: the re-fit clamps a band-fitting frame above the dock again
    (diaanchor read y 200 for 220).
32. **The poll compares against the place the window OPENED at** (dia_made
    primes dia_px/py and the header's x,y,sw,sh; DIA_HF_POS is left as the
    file had it). Design 3.3(b) said "differ from the saved pair"; with no
    saved pair W2 saved unprompted. Now: no write until a drag, an edit or
    the close; an anchored/clamped launch place is not written back unless
    the user moves it or edits.
33. **FERR_NODISK/FERR_IO on the load's read => DIA_S_RDONLY, not DAMAGED**
    (buffer zeroed either way). A walk failure at load stays NOHOME (blocks
    every save, which is the safe answer for an unread file) - the review's
    "do not latch NOHOME on I/O" was not taken: allowing saves after an
    unread walk would overwrite the unread file.
34. **dia_close declines the FIRST close whose remove is refused** (CF=1),
    then lets the next go ahead regardless (SPEC 75.1: never unclosable).
    The not-found case (a row that vanished) also costs one silent decline;
    it cannot be told from "no system disk" by OSAPI_DESK_ITEM's CF alone.
35. **dc_tick re-runs the two today fields** (+ the frame when one is the
    selected day), never the month, and draws nothing under the About card.
    **dc_oncmd folds the card's dismissal into the command's own draw**
    (dc_redo: one dc_all instead of dc_all + dc_regrid).
36. DIA_CAP checked with %strlen (<= 12). Makefile LIVESYSARGS guard tests
    $(filter-out $(APPSYS),$(SYSAPPS)) (DIAARGS made the old test dead).
37. New soak rows diamovecga (MartyPC 5150 CGA via os88marty.machine, the
    GLaBIOS twin) and diaanchor (needs mtools: mcopy puts the fixture DAT on
    the scratch disk); diaregister now asserts NO DESKCAL.DAT after 7 guest s
    untouched (it waited for the unprompted save before); diaswap gains leg D
    (the declined close).

## Wave 3 (appointments and tasks; SPEC.md 107.3, 107.6, 107.9, 107.10 carry the same)

38. **Wave 2's day mark never drew**: glyph 7 is outside the kernel font
    (FONT_FIRST 32..FONT_LAST 126, kernel/font.inc; anything else is a
    space), so no day ever showed a bullet. DC_MARK is '*' (the design's);
    the selection frame's white erase cuts the star's middle row at the mark
    cell's column 0, so dc_reselect re-runs a MARKED old field as it always
    re-ran today's (+1 run, only then).
39. **The entry field takes the HEADING's place** (day heading for an
    appointment, task heading for a task; COMPACT: the one heading row), not
    "the last row of the active pane": an os88line field is 12 rows
    (frame + LN_INSET 3 + the glyph band) and an agenda line is 8. In the
    heading's place it opens with one os88line_draw over pixels already
    ground; on COMPACT its top frame edge IS the rule (same pixels).
40. **Agenda pitch 8, 7 lines TALL / 4 COMPACT** (design: 5 at pitch 10):
    one run a line, no gap fills. Tasks 5 rows at pitch 12 from content 176
    (TALL), **3** on COMPACT (a 12-row box, 36 rows). TALL rows: heading 92,
    agenda 104..159, task heading 164, tasks 176..235.
41. **COMPACT has no tab row**: the heading row is the day's or ` Tasks`;
    Tab, a click on it, or Calendar > Show Tasks/Show Day swaps (greyed
    `(both shown)` on TALL).
42. **The task label is the calendar's own 8-aligned run** (cells 2..25);
    os88ui_chk's label pen is 18 px in = font_run's two-pass fallback. The
    box is os88ui_chkdraw / os88ui_chkhit / os88ui_chkmark (the shared
    control), so a toggle redraws only the mark.
43. **Sort key (date, time+1)**: all-day (0FFFFh) wraps to 0 and comes FIRST
    in its day (design: (date,time), all-day last). tools/os88dia.py's
    deskcal() sorts the same way; its selfcheck updated.
44. **A Remind pick changes ONE thing**: the selected timed appointment, or
    (none selected) the default for new ones - the toast says which
    (`Reminder 15 min early` / `Default: 15 min early`). Design set both.
    Added `30 Min Early` and `No Reminder` (payload flags bit 0 = NOREM).
45. **No "Tasks 3 open" count**: it would make every toggle redraw the
    heading too (the task asked for "toggle redraws only the mark").
46. **The payload is checked at load** (dc_check: counts inside the tables,
    appointments in (date, time+1) order); a failure zeroes it and sets
    DIA_S_DAMAGED, so the include's existing `Replace damaged calendar?`
    path guards it.
47. **Menus**: Calendar = New Appointment, New Task, Edit Item, Delete Item,
    Clear Done Tasks, Show Tasks/Day, Go to Today, Previous/Next Month;
    Remind = 6 leads. Every fact-greyed item has a MENU_DIS twin with its
    reason; switching is a store into the item ARRAY (menu_relayout copies
    the array POINTER, so no OSAPI_MENU_SET re-registration).
48. Extra keys: Esc deselects, Space checks the selected task, Tab swaps
    COMPACT's list. A list longer than its rows PAGES (last line = pager).
    A menu pick closes an open field as Esc does; a click outside the field
    is ignored while it is open.
49. **While the Delete question is up nothing can be changed** (dc_busy);
    its Yes fixes the bullet of the deleted appointment's OWN day (the user
    may have navigated) and shifts a later selection's index.
50. deskcaltask counts runs with a breakpoint on the PACKAGE's dc_run
    (flat address = segment*16 + the offset from a NASM `[map symbols]` of
    the same source, refused unless byte-identical to build/deskcal.bin):
    font_run_x is also the kernel's own (menu.inc, desk.inc), and an idle
    window read 1 on it.
51. Sizes: DESKCAL image 6,080 -> 12,160, bss 2,748 -> 2,860, lz4 5,343 ->
    10,411; os8088-360 321 -> 326 of 354 clusters (28 left), small360
    270 -> 275. DC_BSS 112 -> 224 (DIA_BSS moves; dia_st's offset follows,
    tools/os88dia.py DESKCAL_DIA_BSS = 224, t_dia G holds it).

## Wave 3 review fixes (w3-fix)

52. **DIA_ONTICK answers CF = 1 = hold the save** (os88dia.inc, dia_beat).
    The design (and W3's SPEC 107.3) priced an edit's save as "one write of
    the whole file"; it is SPEC 107.6's 2 reads + 6 writes, ~3.2 s with the
    gfx lock held, and dia_touch made it land at the next 2 s poll - in the
    middle of a typed word. The calendar holds it while the entry field or
    its Delete question is open and until one poll passes with no edit
    (dc_touch sets dc_tq; dc_tick answers), so a burst of ticked boxes is
    one save. A contract addition to the shared include, not an ABI change:
    the only DIA_ONTICK is dc_tick.
53. **A check box's toggle is the calendar's own 6x6 fill (dc_tmark), not
    os88ui_chkmark.** chkmark is the shared glyph whole and its published
    contract (13.15.1) clears the box before the frame and mark, so the
    frame's 44 pixels were written twice per toggle. The hit is
    os88ui_bhit on the box's rect.
54. **The rows on the glass count to the pager** (dc_shown), and a page is
    re-derived after a delete (dc_delans) and a layout change
    (dc_onresize): a short last page left the pager un-erased, and a delete
    that ended paging left the list starting mid-table with no pager.
55. **New Appointment / New Task / Edit Item grey `(typing)` while the field
    is open** (dc_menulive, called by dc_fopen/dc_fclose) instead of closing
    it (losing the typed line) and drawing the heading band only to draw the
    new field over it.
56. A click in the open field moves the caret on the glass (caroff/caron of
    the two cells, dos_caret_to's shape).
57. tools/os88dia.py deskcal(): an appointment's REMIND defaults to "timed",
    as the package creates it; all-day has none.

## Wave 4 (reminders, kern_small end to end, measurement; SPEC.md 107.3, 107.5, 107.6, 107.7, 107.9, 107.10 carry the same)

58. **The chirp's second note rides a once-only sooner beat**: OSAPI_SND_TONE
    is one channel / one request, the DIA beat is 36 ticks, so os88dia.inc
    gained `dia_nx` (1 bss byte): DIA_ONTICK stores n and dia_tick re-arms n
    ticks out once. The worker arm cannot honour it (its next 36-tick sleep
    has begun before the beat runs), so kern_small chirps ONE note, 880 Hz
    for 8 ticks (design: 880 then 660 everywhere).
59. **The agenda line is redrawn BEFORE the alert is created** (design order:
    toast, alert, line): a draw outside W_PAINT clips to the window's visible
    region as computed by OSAPI_WM_CLIP_SET at that moment; an alert created
    first would be inside a region computed before it existed.
60. **The alert drops its `Reminder ` prefix when 9 + 6 + text > 34**
    (OS88UI_AMAX; text > 19 chars), so the user's text is never what
    os88ui's clip cuts. Design only said "<= 34 characters".
61. **Missed at launch = moment strictly BEFORE this minute**; one due this
    very minute is the first beat's to fire (dc_lnow starts 0FFFFh). Design
    "due-but-unfired" would report a 09:30 launch's 09:30 item as missed.
62. **dc_stale: a reminder ARMED for a moment already past is FIRED at
    once, silently** (the review: past = strictly before this minute, as for
    a launch; one armed for THIS minute fires at the next beat with its
    alert) - new timed appointment, an edit that moves
    the time / gives an all-day one a time, the mark-column toggle, a Remind
    pick. Not in the design; without it a 09:30 appointment typed at 10:00
    went off the instant it was entered.
63. **Nothing fires while the entry field is open** (as well as while any of
    our alerts is up - os88ui_awin, the shared gate). An alert takes the
    keyboard; the rest of a typed line would go to it. (The first build held
    the firing's owed save while the alert was up; the review dropped it -
    item 68.)
64. **No-RTC toast is `No RTC - check the time`** (design `Clock not set (no
    RTC)`): CLK_GET's AL bit 0 says no chip, not whether the clock was set
    this session - say the fact (SPEC 47). Said at launch only with a
    reminder still to come and nothing missed (one toast slot; the missed
    count wins).
65. **A reminder's moment is max(0, time - lead)** - clamped at midnight, not
    reaching into the previous day.
66. **No CLAUDE.md document-table row** for docs/plans/completed/DIA-PLAN.md:
    checkdocs does not require one and most completed plans are not listed;
    SPEC.md 107 names the plan file instead.
67. **deskcalperf's times are MartyPC's cycle counter, first call to last**:
    54 calls in 159 ms (TALL) is ~0.25 ms a glyph cell, which agrees with
    PERFORMANCE.md Set 86 (a 27-cell font_run 8.4 ms on MartyPC's 5150) and
    not with Part 2's older field price of 905 us a cell. The CALLS are the
    unit the budget is kept in; the spans are reported as measured.

## Wave 4 review

68. **A reminder's alert holds no save.** Holding the firing's save until
    the OK held every edit and drag made meanwhile for as long as the alert
    stayed up, and a Restart closes nothing, so all of it was lost. The
    save waits for the one quiet poll every edit waits for and lands under
    the alert (3 reads + 8 writes, ~4.4 s on the lock; an OK clicked then
    waits behind it).
69. **dc_stale: past = strictly BEFORE this minute.** A moment equal to now
    (`0930`, 15-minute lead, typed at 09:15) is a reminder now: dc_lnow is
    forgotten and the next beat fires it with chirp and alert.
70. **The FIRED marks owe saves the user did not cause** - a launch that
    missed reminders and an unattended firing. SPEC 107.5.2's "nothing is
    written that the user did not cause" names the exception; 107.6 and
    PERFORMANCE.md Part 5 price both saves, and deskcalperf counts them at
    the drive. Not saving them would re-toast the same missed reminders at a
    relaunch.
71. **No `make test-snd` / sndcheck arm**: the tone is asserted at the
    SND_TONE cell's registers on MartyPC (design 8.x's QEMU arm not built).
72. **No `make diashot` / docs/dia/ PNG set in waves 0-4**: it belongs to
    the pull request.
73. **The calendar's save is 3 reads + 8 writes at the drive** (4 + 8 after
    a missed launch, deskcalperf), not the probe's 2 + 6 that wave 3's text
    quoted for it: ~4.4 s at 400 ms a call.

## Completeness pass

74. **The launch's toast is the first beat's, not the entry proc's.** A hand
    launch registers the row the last close removed - one SYSTEM.CFG write,
    whose verdict CTRL.DRV toasts - so `1 missed reminder` said from the
    entry proc became `Settings Saved` within seconds. dc_missed stages it
    (dc_ltp) and the first DIA_ONTICK says it and nothing else that beat.
    deskcalremind A re-reads the bar once the row is granted, verified to
    fail with the old order.
75. SPEC 107.5 states the registration's and the close's `Settings Saved`,
    the silent autostart, and the live CD's unhandled refusal (§80.3).
76. The live media's getruncpm `--reserve` list prices DESKCAL's clusters
    ($(DIAAPPS)).
