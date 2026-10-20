# Desktop shortcuts: drag an entry out of a Disk window, keep it across a reboot

**BUILT. SPEC.md 26.8 is the contract**, and this file is the design record
behind it: the plan as it was written against `fb544b1`, the owner's
decisions of 2026-10-01 (§9), and, directly below, where what was built
differs from what was planned. Figures in §2 to §8 marked ESTIMATED are
the estimates the plan was written against. The measured ones are in
"What was built".

## What was built, and where it differs from the plan

**Measured against `elendilon` at `fb544b1`:**

- **Resident on kern_big: `.cold` +924, `.text` +68, `.bss` +10 = 1,002
  bytes**, inside the 1 KB budget.
- **kern_small: byte-identical.**
- `.ovl`: +293 bytes, which is not resident.
- `CTRL.DRV`: 8,781 bytes grew to 10,322.

The first build came in at 1,389 resident bytes. Getting to 1,002 took six
changes, each one a refinement of §7.1's placement B rather than a different
design:

| change | what moved or went |
|---|---|
| the drag's tracking loop | into `CTRL.DRV`. Only `fm_drag`'s four-pixel wait stays resident, now `fm_dgwait`, shared with the Disk window |
| the claim bookkeeping | into `CTRL.DRV` and the boot reader, behind one resident heap door, `sc_mem_f` |
| the badge | stamped into the picture at the drop, so the paint does nothing for it |
| the caption's centring | stored as one byte (`SC_R_CAPX`), so the paint measures nothing |
| path and name | one field holding the whole path, so `sc_open` walks one string |
| the three module entries | one entry, `CPE_SC`, with the operation in AH |

**Differences from the text below:**

- **The record is 128 bytes, not 124.** The cell is two bytes (column, then
  row) rather than a packed nibble pair. The separate name field is gone,
  folded into the path. A caption-pen byte was added. Eight records fill one
  KB exactly.
- **The trailer's footer is at the END of the file** (§3 put the header first),
  so the reader finds it without walking the settings records.
- **The trailer is written from a claim of its own, PINNED, for the length of
  the write**, not from a bigger `cpc_buf`. `CTRL.DRV`'s bss is only the slack
  in its own KB rounding (kernel.asm's `MODC_BSS` assertion), and the rows do
  not fit there. `cpc_buf` itself moved into the image on this build for the
  same reason.
- **The grid pitch is 104 pixels, not 72.** That makes room for a full 12-glyph
  8.3 name or header name. It still gives the CGA 4 × 4 = 16 cells.
- **A move that is dropped on the dock or the menu bar does not refuse.** It
  snaps to the nearest free cell, like any other drop. A CREATE dropped
  there still does nothing, as §5.1 says.
- **Enter opens any desktop zone**, drives included (it is the double-click),
  at no extra cost. It was asked for after this plan was written.
- **`tools/os88ui.py`'s `menu_pick` had a stale-greying bug.** It read an
  item's MENU_DIS state before pressing the bar, but `ui_loc_gate` swaps
  Locator's items ON that press. It reads the state after the drop now.

**`tests/desksc.py`** drives every route on MartyPC. It passes on the VGA XT,
on the CGA 5150 and on the Hercules 5150.

The ask:

- Drag any file or folder from a Disk window onto the desktop and get a
  shortcut there.
- The shortcut snaps to a grid and survives a reboot.
- Click selects it, by inversion, exactly like a drive zone. Double-click
  does what a double-click on the same entry in its Disk window does. A
  target that is not reachable gives a toast.
- It persists in `SYSTEM.CFG` and costs nothing there when there is no
  shortcut.
- Its icon comes from the machine-wide icon store, with a tiny "shortcut"
  picture when no icon is available.
- **At most 1 KB of kernel.**

---

## 1. What the tree already has, and what it does not

Each row was read out of the tree. The surprises are in bold.

| need | what exists | where |
|---|---|---|
| a desktop zone that is not a volume | the Wire's SERVICE zone: one record, five one-compare arms in `desk.inc`, damage mask bit 8 | SPEC.md 26.7, `desk.inc:1431` |
| selection by inversion | `desk_sel` + `desk_zone_hilite` (XOR of the hit rect) + `desk_zone_redraw`'s four clip cases | SPEC.md 26.2, `desk.inc:1174-1217` |
| double-click on the desktop | `desk_click_x`, `DESK_DBLT` = 9 ticks | `desk.inc:1038` |
| a drag out of a Disk window | `fm_drag` → `fm_dgdrop`; **a drop where `wm_hit` finds no window is `ret`, a silent no-op** | `files.inc:6456`, `6623` |
| the dragged entry's identity | the clipboard record `fcp_arm` writes at drag BEGIN: drive, parent cluster, type, 8.3 name | `filecp.inc:2033` |
| open a folder in a Disk window | `fm_choose` (AL = vol, DX = cluster, SI = name): fronts, creates, or moves one at the cap | `files.inc:2532` |
| run a package by name | `ld_run_name_x` in the current folder; `ui_sys_open` is the bank → chdir → run → toast → back wrapper | `loader.inc:722`, `ui.inc:2449` |
| open a document by name | `ld_pkg_byname` → `assoc_run_x` (the by-name arm of `OSAPI_PKG_START`) | `loader.inc:1644` |
| a 16×16 icon for an entry | the machine-wide store, SPEC.md 25.9: keyed by (stem, size), **PURGEABLE** (`MEM_P_ICO`, the cheapest rank), dropped all at once | `disk.inc:7533-8300` |
| a toast | `toast_show`, at most 24 characters; `ld_say_status` for `LD_*` codes | SPEC.md 59 |
| persist a setting | SYSTEM.CFG, SPEC.md 51.5 | `driver.inc:564-4810` |

Seven facts shape the design. Four of them contradict a premise of the ask.

1. **SYSTEM.CFG is not sparse on write.** The reader tolerates a sparse
   file. The writer (`CFG_SAVE cpc`, in `CTRL.DRV`) rewrites EVERY key, every
   time, from resident state. Every key also has a FIXED length, and the
   reader refuses a record whose length is wrong. There is no
   variable-length record of any kind. So "costs nothing when absent" is not
   inherited from the format; this design has to provide it (§3).
2. **The read buffer is the file's size cap.** `ovl_cfg_load` calls
   `dskw_read` with `CX = CFG_FBUF` (131 bytes on kern_big), and a file
   larger than that fails with `FERR_BIG` before any I/O. So an OLDER kernel
   handed a SYSTEM.CFG that carries shortcuts loses EVERY setting, not just
   the shortcuts. That only bites on a downgrade: the file sits beside
   `KERNEL.SYS` on the same boot volume. It cannot be fixed from this side,
   and it gets written down in SPEC.md 51.5.
3. **`kern_small` does not read SYSTEM.CFG at all.** `drv_boot_x` is a
   `retf` there and `cp_cfg_save` is a `ret`.
4. **The icon store is 16×16, and it is a cache.** Bodies are 64 bytes,
   16 mask words and 16 data words, drawn by `icon_draw16`. The desktop's
   drive pictures are 32×32 in a different, kernel-internal format. Under
   heap pressure the store is shed WHOLE (`ico_demote`), and only open Disk
   windows re-harvest what they list (`fmv_icorefs`). **Nothing would
   re-harvest a shortcut.**
5. **There is no volume identity.** `BS_VolID` is pinned to `0x88000888` on
   every image and every formatted disk. A shortcut can name a drive LETTER
   and a location on it, and it has to re-check by NAME every time it is
   opened.
6. **There is no shortcut or alias glyph anywhere in the tree.** The
   fallback picture is new.
7. **A drag ARMS A CUT before it knows where it will land.** `fm_drag`
   calls `fcp_arm FCP_CUT` at `.begin`. A drop on the desktop today leaves
   the user's clipboard REPLACED by a Cut of the dragged file. A later Paste
   in another folder would then MOVE the original. That is a latent defect
   in today's tree. It becomes a live one the moment the desktop drop means
   something, so the create path disarms the clipboard (§5.1).

---

## 2. The resident shape

**The bulky state is not resident.** A shortcut table lives in a HEAP
claim, made at boot when the file carries shortcuts or at the first drop.
It is freed when the last shortcut is removed, so a machine with no
shortcuts pays `.bss` and code and no table.

```
SC_MAX   equ 16         ; see 6: a second damage-mask word (decided, +~25)
SC_REC   equ 124        ; the record below, body included (4.2 option I)
claim    = ONE claim for all of them, grown in 1 KB steps as rows are needed
           (8 rows a KB): 1 KB for 1-8 shortcuts, 2 KB for 9-16, and shrunk
           back the same way when shortcuts are removed. Never a claim each.
```

The claim grows through `mem_regrow`, which is the same door every other
growable claim uses; its relocation proc (§3.1 step 4) covers a regrow that
moves it.

### 2.1 The record

| off | len | field |
|---|---|---|
| 0 | 1 | volume index (0 = A:). 0xFF = a free row |
| 1 | 1 | grid cell: column in the high nibble, row in the low nibble |
| 2 | 1 | kind: 0 document, 1 package, 2 folder (the §19.1 type word's low byte) |
| 3 | 1 | flags (reserved; 0) |
| 4 | 32 | the PATH of the parent folder, `\APPS` style, NUL. 32 is `PTH_MAX`, the same cap as `FS_PATH` |
| 36 | 12 | the entry's 8.3 name, NUL-padded |
| 48 | 12 | the CAPTION, NUL-padded, decided at the drop (§5.2) |
| 60 | 64 | the icon body (§4.2 option I) |

**A path and not a cluster, and that is a deliberate choice.** A
(volume, cluster) pair is cheaper to resolve: one `dsk_chdir_q` and no
walk. But it means *this exact copy of this disk*. The six shipped 360 KB
disks and their 1.44 MB twins all carry `APPS\` with different clusters
under it, so a cluster shortcut to `B:\APPS\CALC.O88` breaks the first time
the user puts a different apps disk in B:. With no volume identity (fact 5),
the path is the only name for "the same file" that a floppy machine has. It
costs:

- ~40 bytes of resolver: walk components with `dskw_stat_x` and step in with
  `dsk_chdir_q_x`;
- one directory read per component, which only happens on a double-click.

DECIDED: the path form (§9 Q10).

The path has a cap: an entry whose folder path exceeds 31 characters is
refused at the drop with a toast, `Path too long`. A drive ROOT is the
empty path.

---

## 3. Persistence: a TRAILER after SYSTEM.CFG's terminator

The settings records are unchanged. **The shortcut block follows the
`dw 0` terminator** and exists only when there is at least one shortcut,
which is the "zero bytes when absent" property this format does not
otherwise have:

```
+0    'O88CFG',0,0  dw 3            ; unchanged
+10   records ... dw 0              ; unchanged - every existing reader stops here
+T    dw 'SC'  db ver  db count     ; ONLY when count > 0
+T+4  count * SC_REC                ; the resident table's own rows, verbatim
```

**Verbatim rows are the whole trick.** The reader does not parse a
shortcut. It copies `count * SC_REC` bytes into the claim, and the writer
copies them back out. A key/len record per shortcut was considered and
refused: it would need a second reader arm for repeated keys and buys
nothing. `ver` guards the row layout, the way a settings key's `ver` does
(SPEC.md 51.5). A wrong `ver`, a wrong signature or a short read
means no shortcuts, never an error.

### 3.1 The reader: in `.ovl`, which costs nothing on disk

`.ovl` rides the stage-2 blob, which is a fixed 10 plain sectors read in 2
calls on every geometry (`t_blobruns`). It has **308 bytes spare on
kern_big**, and bytes added there change no read at all. The reader's
addition is ESTIMATED at ~70 bytes:

1. Before reading, `dskw_stat_x SYSTEM.CFG`: DX:CX = its size, from a
   directory the mount already holds, so no I/O.
2. If size ≤ `CFG_FBUF`, take today's path exactly.
3. Otherwise:
   - `mem_claim` the shortcut table, sized from the file in whole KB: the
     trailer is `count * SC_REC`, so 1 KB for up to 8 shortcuts and 2 KB
     for 9 to 16, and the settings half needs no room of its own (next
     step);
   - `dskw_read_x` the WHOLE file into it, with capacity = the claim. That
     is ONE `dskw_read` call. **What it costs in `int 13h` is TO VERIFY
     before the reader is written, not assumed.** A file of 2 sectors (≤ 8
     shortcuts) is one 360 KB cluster. A full 16 is ~2.1 KB: 3 clusters at
     360 KB and 5 sectors at 1.44 MB. That is one call only if
     `dskw_write_sys` allocates the run contiguously AND the read coalesces
     it. If either is false, the fix is in the writer (allocate the run
     whole) and not in the format;
   - `rep movsb` the first `CFG_FBUF` bytes into `ovc_buf`;
   - run today's deserialiser;
   - walk to the terminator (the deserialiser already finds it), check
     `'SC'`/`ver`/`count`, and move the rows down to the claim's base.
4. `mem_movable` the claim with a relocation proc that stores the new
   segment in `[sc_seg]`. That is `ico_reloc`'s shape, ~8 bytes of `.text`,
   and HEAP-UNPIN's rule: a boot claim is a wall otherwise.

**No new `int 13h` comes from the reader.** A boot WITHOUT shortcuts runs
exactly today's path plus one `dskw_stat` that touches no disk.

### 3.2 The writer: in `CTRL.DRV`, which is not resident

`cpc_save` already rebuilds the file whole from resident state. It gains:

- after `cpc_ser`'s terminator, `if [sc_seg]`, emit the 4-byte header and a
  `rep movsb` of the live rows, compacted;
- `cpc_buf` grows by `SC_MAX * SC_REC` in the module's own `.modcb`.

ESTIMATED ~40 bytes of module code and ~340 of module bss: **zero resident
bytes**, paid only while `CTRL.DRV` is loaded.

### 3.3 When it writes

A shortcut must survive a power-off, so "at the next panel close" is not
good enough. Every create, move and remove ends in an immediate write
through the resident `cpf_cp_flush_close` thunk, after setting
`[cp_wdirty]`. Two cases change that, and both already have the shape that
handles them:

- **The Control Panel is open.** `cpf_cp_flush_close` ends in an
  unconditional `mod_drop MOD_CTRL`, and that would free a live panel's code
  (`mod.inc:550`). So the shortcut path tests for a live panel and only sets
  `[cp_wdirty]`; the panel's own close writes. ~8 bytes.
- **The boot disk is not in its drive.** `cpf_need` fails, the existing
  toast says `Not Saved: No Sys in A:`, and `[cp_wdirty]` stays set. The
  next panel close, Restart or hibernate retries it. The shortcut is live
  for the session either way.

The write costs ~5 `int 13h` (directory, FAT, data, directory, FAT), which
is ~2 s on a 4.77 MHz XT. It is paid only at a gesture the user just made,
never at boot or at paint.

---

## 4. Icons

### 4.1 Where the picture comes from at the DROP

The dragged entry already has its body in the window: `fmv_get_icon` on
its listing index returns the 64-byte body or all-zero.

- A **folder** is `dsk_folder_ico`, resident, by kind. It needs no body.
- A **document** is the composed page + 8×8 glyph (SPEC.md 54.3). It is
  already a 64-byte body in that window's store row.
- A **package** is its harvested icon, or zero.

### 4.2 Where it lives AFTER the drop: the one real fork

The ask says to reuse the global icon store. The store is a CACHE (fact
4), and the two readings of "reuse" have different failure modes:

| | **I. the body travels with the shortcut** (recommended) | II. the shortcut keys the store | III. II, with the store made a HELD claim |
|---|---|---|---|
| what the record holds | 64-byte body | (stem, size) key, 10 bytes | (stem, size) key |
| boot | rows land in the claim; done | `ico_need` + `ico_put` per shortcut: **a 4 KB claim at every boot that has a shortcut**, today made lazily | as II |
| after the heap sheds the store | unaffected | **every shortcut falls back to the tiny glyph** until reboot, or until a Disk window happens to list that package again | cannot happen |
| heap | +64 a shortcut, inside the same 1 KB claim | -54 a shortcut, +4 KB store pinned live from boot | **4 KB NEVER purgeable** on every machine with one shortcut; docs/reports/DOS-GAMES-2026-09-16.md lost a game by 3 KB |
| resident code | `icon_draw16` from a staged copy: ~15 | key match + `ico_find` per paint + boot seeding: ~45 | II + a purge-rank switch: ~60 |
| what is reused | the store as the SOURCE at the drop; the format and the draw routine always | the store as the only home | as II |

**DECIDED: I.** The shortcut persists its own picture, which is what
"embedded icon data" in the ask already implies. It never degrades. It costs
no kernel bytes beyond the draw, and its 64 bytes of heap a shortcut sit in
the one shared claim (§2). II re-introduces exactly the degradation I avoids,
and III spends 4 KB of every shortcut user's heap to prevent it.

### 4.3 The shortcut badge, which is also the fallback

The badge is an 8×8 arrow in the 16×16 cell's lower left, on EVERY shortcut,
System 7 alias style (§9 Q5: decided, because it is cheap):

- The body is staged into `dsk_ico`'s scratch, as every draw already does.
- The badge's eight rows are OR'd into the low-left byte of the mask rows
  and written into the data rows.
- The result is drawn with `icon_draw16`.

ESTIMATED 8 bytes of glyph + ~20 of overlay. **The fallback costs nothing
more**: a body that is all zero at the drop (a package with no icon, or a
store that had been shed) is the same staging with nothing under the badge,
so the picture is the badge alone, which is the "tiny shortcut icon" of the
ask. Folders draw `dsk_folder_ico` under the badge.

### 4.4 Size on the desktop

A shortcut draws its 16×16 icon centred in a cell, with its caption below,
like a drive zone. **It is not pixel-doubled to 32×32.** A doubler is a
256-byte stage and ~40 bytes of code, and on the CGA, whose drive pictures
are 14 rows tall (SPEC.md 26.4), a doubled 16-row picture is 32 tall in a
34-row step. The Disk window's own icon view is 16×16, so the desktop
matches what the user dragged. DECIDED: 16×16 first; both are to be looked at
once it is in (§9 Q4).

---

## 5. Gestures

### 5.1 Create: a drop on empty desktop

`fm_dgdrop`'s `jz .out` (no window under the point) becomes `jz .desk`:

1. **Refuse** silently, as a drop on nothing does today, if the point is:
   - on the menu bar or the dock, or
   - inside a drive or service zone's rect, or
   - not on the primary display: the extended desktop gets no shortcuts.
2. **Snap to the NEAREST FREE cell** (§9 Q7, decided), so the user never
   has to aim at a cell edge:
   - cell = ((x − origin) / pitch, (y − `MBAR_H`) / step), rounded to the
     nearest cell;
   - if that cell is taken or blocked (the drive columns), test the ring of
     cells around it, distance 1, then 2, and so on, taking the first free
     one by smallest distance.

   With ≤ 16 shortcuts the search is short. A desktop with no free cell at
   all toasts `No room`. This runs in `CTRL.DRV` (§7.1), so it costs no
   resident byte, and the move gesture (§5.3) shares it.
3. **Fill a free row**:
   - volume = `[fcp_cbdrv]`;
   - parent path = the source window's `FS_PATH`;
   - name and kind = the clipboard record;
   - body = `fmv_get_icon`;
   - caption = §5.2's rule.

   If there is no free row (`SC_MAX` reached), toast `Too many shortcuts`.
   If the claim has no free row, `mem_regrow` it by 1 KB first; if there is
   no claim yet, `mem_claim` one.
4. **Disarm the clipboard**: `[fcp_cbop]` = none (fact 7). It is one store,
   and it fixes today's latent defect as well.
5. `desk_zmark` the new zone and write (§3.3).

### 5.2 Select and open: `desk_click_x`

A shortcut is zone index `DESK_SC0 + i`, where `DESK_SC0` = `DESK_NZ`
(9 on kern_big). Every routine in `desk.inc` is already asked about an
INDEX, which is what let the Wire's zone cost five one-compare arms. A
shortcut zone gets the same arms in:

- `desk_zone_rect`: from its grid cell, not from `desk_ord_xy`;
- `desk_draw_zone`: an icon16 and a caption;
- `desk_zone_label`: the record's CAPTION field, so painting decides
  nothing;
- `desk_click_x`: hit rect and double-click dispatch;
- `desk_dmg_zones`: its mask bits.

**The caption is decided ONCE, at the drop, and stored** (§9 Q6, decided):

| kind | caption |
|---|---|
| package | its HEADER name (SPEC.md 20.2, header +16, e.g. `Calculator`), **if** `font_width` of it fits the cell pitch less 8 px; otherwise the 8.3 stem without `.O88` |
| document, folder | the full 8.3 name |

The header name costs one `dsk_peek_x` of the package's first sector at the
drop: one `int 13h`, inside a gesture that is about to spend ~5 more on the
write. It runs in `CTRL.DRV`. Deciding at the drop rather than at paint is
what keeps the measuring out of the paint path, and what keeps a long name
from being re-tested on every repaint.

`desk_sel` and `desk_zone_hilite` serve it unchanged, so selection inverts
exactly as a drive's does, with §26.2's flip-not-repaint under a covering
window. **A shortcut does not move when a volume mounts**, so it is outside
`desk_zones_dmg`'s ordinal extent; its arm is a fixed rect.

**Double-click → `sc_open`**, `.cold`, no gfx lock held: the same context
`desk_click_x` gives `files_open_drive` and `ui_svc_open`. It runs these
steps:

1. Bank the user's place (`osapi_file_here`), as `ui_sys_open` does.
2. Volume row free (`DVK_FREE`, no such drive) → toast `No drive B:`.
3. Quiet-mount that volume at its root. Failure, which is no disk → toast
   `No disk in B:` through the mount's own `FERR_NODISK` path.
4. Walk the path: `dskw_stat_x` each component, require the directory
   attribute, step in. A miss → toast `Not found`.
5. `dskw_stat_x` the name. A miss → `Not found`. The kind must still agree,
   so a folder replaced by a file is `Not found`.
6. Dispatch on kind. **This is what the Disk window does, by name, through
   the doors it already has:**

   | kind | action | why that door |
   |---|---|---|
   | folder | `fm_choose` (AL = vol, DX = the folder's cluster from step 5, SI = name) | fronts a window already showing it, opens one, or moves the frontmost at `FM_MAXWIN` |
   | package | `ld_run_name_x`, then `ld_say_status` on failure | `ui_sys_open`'s own pair |
   | document | `ld_pkg_byname` with the staged 16-byte locator | `OSAPI_PKG_START`'s by-name arm, which `assoc_run_x` drains: the association lookup, `Needs X.O88` and the hand-over. `[assoc_pwin]` = 0, so it reads the home from the globals step 4 just set |

7. Put the user's place back (`ui_tm_back`).

**Nothing is extracted from the file manager.** `fm_open_sel` is welded to
a listing INDEX and its window's cache. Every arm it reaches already has a
by-name twin, and those twins are what other desktop launchers use. So
`sc_open` is a resolver in front of three existing calls.

### 5.3 Move: drag a shortcut

DECIDED, in scope (§9 Q2). `desk_click_x`
sees the button still down after `FM_DRAGMIN` pixels and runs a drag loop.
The loop is `fm_drag`'s shape with its outline routine:

- `fm_dgxor` draws the 96×16 XOR bar. Called with the zone's own rect
  instead, it is a one-register change.
- Drop → nearest free cell (§5.1 step 2; the shortcut's own cell counts as
  free) → store the cell → `desk_zmark` old and new → write.

A drop on the menu bar or the dock leaves the shortcut where it was.

### 5.4 Remove: three routes, one body

DECIDED (§9 Q3): all three of the routes below, and no drag back into a
Disk window. **They converge on ONE module entry, `sc_remove(i)`**, which
does the following:

- frees the row;
- shrinks the claim by a KB when its top KB is empty, and frees it when the
  last row goes;
- clears `desk_sel`;
- `desk_zmark`s the cell;
- writes.

| route | what it costs resident | how |
|---|---|---|
| Locator **`File` → `Remove Shortcut`** | ~30 | A second item in `menu_items_file`. `ui_loc_gate` already swaps item 0 between `Close Window` and its `MENU_DIS` twin on every drop of the bar; it does the same for item 1 on "is `desk_sel` a shortcut zone". The twin costs ONE byte, not a second string: `menu_s_closex db MENU_DIS` sitting immediately before `menu_s_close` is the idiom already in `menu.inc:2608`. Plus one `CMD_*` id and one arm in `ui_cmd`'s compare ladder. |
| **right-click** a shortcut | ~45 | `ui_rdown`'s `.strip` arm today gives the bare desktop nothing (`ui.inc:1258`). It gains: a shortcut zone under the press → select it (the same lock-held `desk_sel` store `desk_click_x` makes) → `menu_popup` a one-item descriptor whose item is the same `Remove Shortcut` string → the pick calls `sc_remove`. `menu_popup` exists and already serves both buttons (`menu.inc:1759`). The rule that a right press must not stamp `[ui_click_t]` holds as written. |
| select + **Delete** | ~18 | The key path sends keys to `wm_top` only, so the desktop gets them nowhere today. The arm is: when Locator owns the bar (`[menu_win]` = 0, which a click on the bare desktop makes so) and `desk_sel` is a shortcut, scan code 0x53 calls `sc_remove`. Any other key, or any window owning the bar, is today's path untouched. |

The right-click route's cost is what was asked about ("if right click isn't
too expensive"): about 45 bytes, because the popup machinery and the
selection store both exist. It is in. A second popup item, `Open`, would be
~8 more; it is left out unless asked for.

---

## 6. Limits that fall out

- **`SC_MAX` = 16 on kern_big** (§9 Q8, decided). The damage mask was ONE
  word (`wm_dmg_zn`; `desk.inc:950` asserts at most 16 zones), and the 8
  volume zones and the Wire take 9 bits of it. Shortcuts get a SECOND word
  of their own, so shortcut `i` is bit `i` of it:
  - `desk_dmg_zones` answers it in DX beside AX;
  - `wm_paint_dmg` stores it;
  - `desk_paint_mask` walks it.

  ESTIMATED +25 bytes, and the `%error` at 16 then guards each word
  separately. `desk_sel` stays a byte: zone indices run to 24.
- **The grid** shares the drive column's pitch so the two line up:
  - row step = `[desk_zstep]`: 60 on VGA and Hercules, 34 on the CGA;
  - column pitch = `DESK_COLW` (44) is too narrow for a caption. An 8-letter
    stem is 64 px, so the pitch is 72.
  - At 72 × 60: VGA has 7 columns × 7 rows left of the drive column,
    Hercules 8 × 4, and CGA 7 × 4.
  - 4 bits a coordinate suffices on every adapter. The cells under the drive
    columns are refused (§5.1).
- **The extended desktop is out.** A shortcut lives on the primary display.
- **`kern_small` is out** (§9 Q1, decided). It reads no SYSTEM.CFG at all
  (fact 3). Every arm is `%ifndef KERN_SMALL`, as the Wire's are, so
  kern_small must build BYTE-IDENTICAL, and that is a gate (§8). `kern_emu`
  inherits kern_big's.

---

## 7. The bill against 1 KB

Every figure in this section is ESTIMATED.

### 7.1 Two placements

`CTRL.DRV` is ALREADY loaded at the end of every create, move and remove,
because that is where the writer lives. So the bodies of those three
gestures can be module entries in it, and their resident cost falls to a
thunk each. ONDEMAND-PLAN's test is met: the gesture is only worth making
if it can be persisted, and persisting needs the system disk.

| piece | where | A: all resident | **B: gestures in `CTRL.DRV`** (DECIDED) |
|---|---|---:|---:|
| zone arms: rect, draw, label, click | `.cold` | 220 | 220 |
| second damage-mask word (16 shortcuts) | `.text`/`.cold` | 25 | 25 |
| `sc_open`: resolver + 3-way dispatch + toasts | `.cold` | 180 | 180 |
| badge on every icon, which is also the fallback | `.cold` | 28 | 28 |
| drop → create: nearest-free snap, header-name caption, fill, regrow, disarm | `.cold` / module | 280 | 30 |
| move: drag loop / commit | `.cold` / module | 110 | 70 (the loop stays; the commit moves) |
| remove: `File` menu item + gate + `ui_cmd` arm | `.cold`/`.text` | 30 | 30 |
| remove: right-click popup | `.cold`/`.text` | 45 | 45 |
| remove: Delete key | `.text` | 18 | 18 |
| remove: the body (free row, shrink claim, mark) | `.cold` / module | 50 | 0 (module) |
| write trigger + live-panel guard | `.cold` | 30 | 15 (folded into the module call) |
| module entries: 4 `.bss` + thunk each, x3 (create, move, remove) | `.bss`/`.cold` | - | 50 |
| `.bss`: `sc_seg`, `sc_n`, `sc_kb` | `.bss` | 4 | 4 |
| claim relocation proc | `.text` | 8 | 8 |
| strings not already in the tree (`Remove Shortcut`, ~4 toasts) | `.text` | 70 | 70 |
| **resident total** | | **~1,100** | **~795** |
| boot reader | `.ovl` (308 spare, no disk cost) | 70 | 70 |
| writer + gesture bodies | `CTRL.DRV` (not resident) | 40 | 450 |

**DECIDED: B, at ~795 resident, ~230 bytes under the 1 KB bar.** A no
longer fits once the decided features are added, so it is not an option.
Nothing touches `.lowbss`, which has 34 bytes left. The cold rung currently
has 497 bytes before its next crossing, so this crosses it, and per
CLAUDE.md that is the bytes' business and not the rung's.

**What B costs in behaviour, stated rather than discovered.** A create, move
or remove loads `CTRL.DRV` from the BOOT volume at the moment of the
gesture:

- On a hard-disk boot, or a two-drive machine with the system disk in A:,
  this is invisible.
- On a ONE-drive machine with a data disk in A:, the gesture REFUSES with
  the existing `Needs Sys Disk A:` toast (SPEC.md 2.8.4), and nothing
  changes on the desktop.
- Opening a shortcut needs no module and works with any disk in A:.

The refusal is the price of the ~300 bytes B saves. It is consistent: the
same machine could not have SAVED the shortcut either. It is §9's one open
question.

### 7.2 The one `int 13h` this cannot avoid

The question asked was whether the BOOT PARSING costs another `int 13h`.
It does not (§3.1). **The resident code does, on the 360 KB disk.**

- `KERNEL.SYS` on `os8088-360.img` ends on the LAST sector of cylinder 9,
  with 48 bytes free in that sector.
- Resident bytes grow the packed tail at about 0.8×. So past ~60 resident
  bytes, every 360 KB boot reads one more cylinder run: one more `int 13h`,
  ~400 ms on an XT, on every CPU.
- The 720 KB, 1.2 MB and 1.44 MB disks have 7 to 16 sectors of headroom in
  the run and pay nothing.

This is not specific to shortcuts; any resident feature over ~60 bytes pays
it next. DECIDED (§9 Q9): accepted. It is recovered by a later size pass,
or not, as the tree keeps growing either way. The commit that crosses it
says so.

---

## 8. Gates (to write with the code; docs/WRITING-TESTS.md)

Each gate is to be broken on purpose and watched go red before it counts.

| row | asserts | how it goes red |
|---|---|---|
| `scdrop` (soak) | drag `B:/APPS/CALC.O88` onto the desktop: the zone appears at the snapped cell; `[fcp_cbop]` is disarmed; SYSTEM.CFG on A: carries the trailer | omit the disarm; omit the write |
| `screboot` (soak) | `system_reset`; the zone comes back at the same cell with the same body; settings are untouched | read the trailer with the wrong `ver` |
| `scopen` (soak) | double-click each kind: a folder fronts or opens a Disk window on it; a package runs; a document opens in its program | swap two dispatch arms |
| `scmiss` (soak) | B: empty → `No disk in B:`; file renamed → `Not found`; volume row free → `No drive` | read the toast cell out of guest memory |
| `scremove` (soak) | each of the three routes removes the selected shortcut and only it; the `File` item is greyed with no shortcut selected; Delete with a WINDOW owning the bar reaches the window, not the desktop; a right press never composes into a double-click | point `ui_loc_gate`'s swap at the wrong item |
| `scgrid` (soak) | a drop on an occupied cell lands on the nearest free one; a desktop with every cell taken toasts `No room`; the 17th shortcut toasts `Too many shortcuts`; the claim is 1 KB at 8 and 2 KB at 9 | drop the ring search |
| `t_scsmall` (fast) | `kern_small` builds byte-identical to its blessed baseline | leave one arm outside its `%ifndef` |
| `t_sccfg` (fast, host-side) | `tools/` round-trips the trailer format against the SPEC.md table; a file with no shortcuts is byte-identical to today's | change `SC_REC` in one place only |

All of these run on MartyPC, `os88ui` driven, `os8088_xt_vga` and a 1bpp
twin. A drawing change is not done until it has been looked at on the CGA
(SPEC.md 26.1).

---

## 9. Decisions, and the one question left

The owner answered on 2026-10-01:

| # | question | DECIDED |
|---|---|---|
| Q1 | which kernels | `kern_big` (and so `kern_emu`) only |
| Q2 | move after creation | yes (§5.3) |
| Q3 | how to remove | Locator `File` → `Remove Shortcut`, right-click, and select + Delete; NOT a drag back into a Disk window (§5.4) |
| Q4 | 16×16 or 32×32 | 16×16 first; both are looked at once it is in |
| Q5 | badge | on every icon, because it is cheap; it is also the fallback (§4.3) |
| Q6 | caption | a package's header name when it fits the cell, else the stem; 8.3 otherwise (§5.2) |
| Q7 | occupied cell | nearest free cell (§5.1) |
| Q8 | how many | 16, for a second damage word (§6) |
| Q9 | the 360 KB boot's extra `int 13h` | accepted, recovered later or never (§7.2) |
| Q10 | path or cluster | path (§2.1) |
| Q11 | the store fork | I: 64 bytes of heap a shortcut, all in ONE claim regrown in 1 KB steps, and the bodies persisted in SYSTEM.CFG (§2, §4.2) |

**Open:**

| # | question | recommendation |
|---|---|---|
| Q12 | placement B means a one-drive machine with a DATA disk in A: cannot create, move or remove a shortcut until the system disk goes back in (`Needs Sys Disk A:`); opening one works with any disk. Acceptable, or should the gestures be resident and the save deferred to the next panel close or Restart, at ~+300 resident (~1,100, over the 1 KB bar)? | accept: a shortcut made there could not have been saved anyway |
