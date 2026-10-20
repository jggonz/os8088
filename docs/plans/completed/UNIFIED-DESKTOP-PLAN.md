# One desktop: every icon is an item in one slot grid

**Status: BUILT, all five waves. SPEC.md 26.9 is the contract and this file
is the design record behind it.** Written on branch `unified-desktop`, cut
from `desktop-shortcuts` at `9a760ab`. Every figure marked MEASURED is a
symbol span off that commit (`tools/os88sym.py`, both kernels). Every figure
marked ESTIMATED is a sketch's count, and section 7 is what the build
actually measured.

The ask, in the owner's words:

> Right now the newly implemented shortcuts and the drive icons are
> different. You can position shortcuts but not drives. They seem to run
> through different paths. I'd like them to be the same. When something
> wants to add something to the desktop it can call to do so, and it either
> goes at the given slot or the first available slot (or remove an item).
> That should let drives work the same as today if there is nothing placed
> there and naturally flow around if there is. Simplify by having one
> consistent interface both for the user and system, and let's future apps
> place something on the desktop. Icons can either draw from a given source
> (like the kernel icons) or the cache (like the shortcuts).
>
> Target: around 400 bytes saved, or more. ABI/API changes allowed.

## 0. The answer in one paragraph

The desktop becomes **one grid of SLOTS** and **one table** that says which
item sits in which slot. Slot `s` is a fixed cell. The first-free order is
column-major from the top-right corner, which is exactly the order drives
flow in today. So with nothing placed, `A:`, `B:`, `C:` and the Wire land
where they land now, and with something placed they flow around it. Every
item answers the same four questions:

- where is it: its slot;
- what does it look like: one of three icon sources;
- what is it called: its caption;
- what does a double-click do: one of three actions.

One routine answers each question for every kind. One placement rule
serves the kernel, the drivers, CTRL.DRV and packages alike: a placed item
goes in its slot or the nearest free one, and every drive nobody has
placed stays packed, gapless, into the first free cells around it. The public slot `OSAPI_DESK_SVC` (0x03F8) becomes **`OSAPI_DESK_ITEM`**,
which a driver and a package can both call. Every item can be dragged to a
new cell, drives included, and a drive's place is kept in SYSTEM.CFG beside
the shortcuts. **The net is ESTIMATED at about −420 resident bytes on
`kern_big` (range −180 to −420, section 4)**, most of it the shortcut code that
duplicated the drive zones' geometry, plus a smaller saving on `kern_small`.

---

## 1. What the tree has today

Three zone species share `desk_sel`, `desk_zone_redraw` and §26.2's
flip-not-repaint, and nothing else.

| | drives (§26.1) | the Wire (§26.7) | shortcuts (§26.8) |
|---|---|---|---|
| zone index | 0..7, a `dsk_vtab` row | 8, `DESK_SVZ` | 16..31, a claim row |
| where | **an ordinal**, recomputed on every query by walking every volume (`desk_ord`) | the ordinal after the last volume | a stored (column, row) |
| grid | column from the right, 44 px pitch, 32 px zone | same | 104 px pitch from the band's LEFT, 96 px cell, and it **reserves** every column drives could reach |
| picture | `icon_draw_ix` of a kernel run record | the driver paints it (`drv_pkg_call`) | 64-byte body + badge |
| caption | `DV_LBL` or `X:` | `.bss` mirror | in the claim, pen pre-measured |
| geometry code | `desk_ord_xy`, `desk_zone_xy`, `desk_rect_ax` | (shares) | `sc_xy`, `sc_rect`, `sc_caprect` |
| hit / damage / paint | `desk_click`, `desk_dmg_zones`, `desk_pm_body` | (shares) | `sc_hit`, `sc_dmg`, `sc_paint_mask`, and a second mask word |
| added / removed | `desk_zmark` + a high-water mark (`desk_zhw`), because removing a drive **renumbers** every zone after it | same, plus `inc [desk_zhw]` | CTRL.DRV repaints its own cell |
| can be moved | no | no | yes |

**The duplication is MEASURED, kern_big `.cold`:**

| group | routines | bytes |
|---|---|---:|
| A. shortcut geometry that copies the drive zones' | `sc_xy` 40, `sc_rect` 17, `sc_caprect` 37, `sc_hit` 45, `sc_dmg` 46, `sc_paint_mask` 29, `sc_hilite` 41, `sc_draw` 127 | **382** |
| B. the ordinal flow and its repaint debt | `desk_ord` 34, `desk_ord_xy` 39, `desk_zone_xy` 18, `desk_zones_dmg` 77, `desk_zones_shown` 8, `desk_zmark_x` 23, `desk_svflag` 15 | **214** |
| C. what the unification rewrites | `desk_draw_zone` 227, `desk_click_x` 128, `desk_dmg_zones_x` 59, `desk_pm_body` 43, `desk_paint_x` 12, `desk_zone_label` 34, `desk_zone_hilite` 19, `desk_zone_rect` 10, `desk_rect_ax` 15, `desk_zflag` 13, `desk_zones_paint_x` 20, `osapi_desk_svc_x` 84 | **664** |
| | | **1,260** |

Group B exists because a drive's position is not STORED. Most of group B's
weight is the high-water mark: removing a volume moves every zone after it,
so the repaint has to cover a rect whose size depends on what was showing
before. Store the position and both the walk and the debt disappear.

---

## 2. The design

### 2.1 One grid

- **One cell**: `DESK_CW` wide, `[desk_zstep]` tall (60, or 34 on the CGA).
  On `kern_big`, `DESK_CW` = 96 and the pitch `DESK_PX` = 104: the
  shortcuts' twelve-glyph caption, as the owner settled it in §26.8.2. On
  `kern_small`, which has drives only, `DESK_CW` = 32 and `DESK_PX` = 44,
  today's numbers, so that kernel draws exactly what it draws now.
- **Anchored on the right.** Column 0 is the rightmost one, and its left
  edge is `[vid_desk_zx]`. That word is still set in the same three places
  (`viddet`, `dock`, `dockmod`), but from a per-build constant
  `DESK_ZXOFF` rather than the literal 56. A right dock moves the grid
  exactly as it moves the drive column today.
- **Slot `s`** is column `s / rows` and row `s mod rows`. Slots fill a column
  downwards, then the next column to the LEFT, which is §26.1's flow.
  `[desk_ncell]` = columns × rows is computed in `desk_rowcalc`. That routine
  already runs at boot, on an adapter switch and on a dock move.

| adapter | rows | columns at 104 | cells |
|---|---:|---:|---:|
| VGA 640×480 | 7 | 6 | 42 |
| EGA 640×350 | 4 | 6 | 24 |
| Hercules 720×348 | 4 | 6 | 24 |
| CGA 640×200 | 4 | 6 | 24 |

`kern_big` can show 8 + 1 + 15 = 24 items, which is exactly the smallest
screens' 24 cells, so every item always has one (decision D3). Creating a
16th shortcut says `Too many shortcuts`, as the 17th does today.

- **The picture box** is `IH` = 32 rows, or 16 on the CGA. Captions sit at
  `y + IH + gap` for every kind, so a row of mixed items lines up. A 32×32
  drive draws at the top of the box, a 16×16 shortcut at its bottom. On the
  CGA, the 14-row drive draws 2 rows down and the 16-row shortcut fills the
  box. `desk_zh1` keeps its meaning (the zone's height minus one); only its
  CGA value changes.

### 2.2 One table: a slot byte per zone

```
desk_zslot  resb DESK_NZ      ; per zone index:
                              ;   0..0x3F  shown, in that slot
                              ;   bit 6    the USER put it there (a pin)
                              ;   bit 7    a pinned item that is not live
                              ;            now: it keeps its place
                              ;   0xFF     nothing
```

Zones are renumbered compactly: 0..7 volumes, 8 the service item,
`DESK_SC0` = 9 for 15 shortcuts, so `DESK_NZ` = 24 on `kern_big` and 8 on
`kern_small`. `SC_MAX` drops from 16 to 15 so that the item limit equals the
smallest screen's 24 cells (owner, D3). That is 25 bytes of `.bss` against today's `desk_zhw` and
`sc_dmgm`.

**The table is the one answer to "where is it, and is it shown".**

- "Is slot `s` taken" is a short scan of 24 bytes.
- "Where is zone `z`" is one load and `desk_cell_xy`.
- Nobody walks the volume table to find a position any more.

A shortcut's record keeps a slot byte too, but only for the FILE. CTRL.DRV
copies it from the table when it writes SYSTEM.CFG, and the boot reader
copies it back.

### 2.3 One item: slot, source, caption, action

| kind | live when | icon source | caption | action |
|---|---|---|---|---|
| volume `z` | `DV_FLAGS` bit 0 (unchanged) | **KERNEL**: `icon_draw_ix` of `desk_pico[DVF_525 / fixed]` | `DV_LBL` or `X:` | `files_open_drive` |
| service | a driver registered one | **DRIVER**: its paint verb, as today | its record | `ui_svc_open` (front by name, else run from `SYSTEM\`) |
| link `z` | its claim row is not free | **CACHE**: its 64-byte body, plus the badge | its record | `sc_open` (walk the path, do what the entry is NOW) |

These are the owner's two sources, kernel and cache, plus the driver's. The
third has to stay: the Wire's picture is the driver's so that a machine with
no network card carries no glyph (§26.7). All three meet in one
`desk_draw_zone` as a three-way switch around **one** cell fill, **one**
caption routine (measured with `font_width` for every kind) and **one**
highlight.

The highlight inverts the **picture and the caption** for every kind, as
shortcuts do today. Inverting a whole 96 px cell would read as a hole in the
desktop. Drives used to invert their 32 px zone: picture, gap and label
band. They now invert the picture and the caption, which is the Mac's look.

A hit is anywhere in the cell, for every kind, as a shortcut's is today.

### 2.4 One placement rule: placed items stay, everything else packs

There are two kinds of position, and that is the whole rule:

- **PLACED**: a link (where it was dropped), and any drive or the Wire the
  user has dragged (bit 6). A placed item never moves on its own.
- **FLOWING**: every drive and the Wire that nobody has dragged. These are
  kept PACKED, in index order, into the first free cells around the placed
  ones. That is today's gapless column, flowing around whatever sits in it.

The packing is one routine, and it needs to know nothing about what was
added or removed (owner, D2):

```
desk_reflow   for every FLOWING zone 0..8: drop its slot.
              for every FLOWING zone 0..8 that is live, in index order:
                  take the first free cell.
              for every zone whose slot changed: mark the old cell and the
                  new cell dirty, and post.
              a PLACED drive that is not live keeps its cell with bit 7;
              when it comes back, it takes that cell if it is free, and
              otherwise it is unpinned and flows.
```

It runs:

- when a volume is added or removed (`osapi_vol_add`/`_del`,
  `dsk_flop_add`, `dsk_fdd_retire`), where `desk_zmark` is called today;
- when the service item is registered or withdrawn;
- when a link is created, moved or removed, so a drive packs into a cell a
  link has just given up;
- from `desk_rowcalc` (adapter switch, dock move), after it drops every slot
  at or past the new `[desk_ncell]`;
- once at the end of `drv_boot`, after SYSTEM.CFG has put the links and the
  pins in. Until then, `[desk_ready]` is 0 and the calls above return at
  once, so the boot order is the index order whichever driver loads first.
  `kern_small` makes that one call at the same point in `kmain`.

**It has to be resident, and it costs almost nothing.** Volumes change in
more places than boot and the Control Panel. `RAMDISK`'s package verb
mounts one (`drivers/ramdisk/rdpkg.inc`), hibernate detaches and reloads
the hardware drivers (`hbm_detach`/`hbm_reload`), and the kernel drops a
driver's volumes when it unloads. A reflow in CTRL.DRV would need the
system disk on every one of those paths. Resident, it is about 45 bytes,
and it IS `desk_place_all`, which the adapter switch needed anyway.

**The repaint is the CELLS that changed.** `[desk_zdirty]` becomes an
8-byte bit set of cells (64 is more than any screen has). `ui_task`'s idle
pass paints each RUN of consecutive dirty slots as one rect. Slots in a
column are numbered consecutively, so a run is exactly a strip of one
column. Mounting a disk that lands at the end of the column is one cell.
Unmounting `C:` above `D:` and `E:` is one strip of three. A cell between
two columns is never painted, so §26.3's phantom column cannot happen, and
`desk_zones_dmg`'s corner arithmetic and the `desk_zhw` high-water mark go.

### 2.5 One public slot: `OSAPI_DESK_ITEM` (0x03F8, replaces `OSAPI_DESK_SVC`)

```
in   AL = 1 add, 0 remove
     add:    ES:SI -> a record in the caller's segment, whose kind follows
             from who the caller is (the kernel can tell, osapi_vol_fence):
       a DRIVER  -> a SERVICE item: today's 39-byte record plus one byte,
                    +39 = the slot wanted (0xFF = any). Not persisted;
                    withdrawn by the driver or at its unload.
       a PACKAGE -> a LINK: a §26.8.1 record (128 bytes: volume, slot,
                    kind, path, caption, 64-byte picture). PERSISTED, so
                    CTRL.DRV does the work and SYSTEM.CFG is written; it
                    needs the system disk, exactly as a drag-created
                    shortcut does (§26.8.7).
     remove: AH = the zone the add returned. A driver may remove only its own
             service item. A package may remove a link.
out  CF = 0, AL = the zone; CF = 1 refused (no room, too many, not a driver
     and not a package, a second service item, a remove of somebody else's)
```

The drag-out of a Disk window then becomes **one caller of the same door**.
CTRL.DRV composes the record from the clipboard, as it does now, and hands
it to the same "insert a link record" body a package's call reaches. A
future installer gets "put my program on the desktop" from one call. The
SDK grows `OS88_DESK_ADD`/`OS88_DESK_DEL` in `apps/os88api.inc`, and
`os88_desk_add` in `apps/cc/os88.h`.

**Resident cost of the package half, ESTIMATED: about 15 bytes.** It is a
branch from the fence into the existing `sc_modcall`, which already exists
for the drop. Everything else is CTRL.DRV's.

### 2.6 One set of gestures, for every item

| gesture | today | after |
|---|---|---|
| click, double-click, Enter | every zone | unchanged |
| drag 4 px, drop on a cell | shortcuts only | **every item**: drives, the Wire, links. The nearest free cell; a pin is set (bit 6) and SYSTEM.CFG written |
| Delete, right-click, `File > Remove Shortcut` | shortcuts | links only. A drive or the Wire refuses, as a drive's zone refuses today. The menu item is renamed `Remove from Desktop` when that is wanted |

Moving a drive needs CTRL.DRV, so on a one-drive machine with a data disk
in A: it refuses with `Needs Sys Disk A:`, as moving a shortcut does now.
Opening never needs the module.

### 2.7 Persistence: the trailer, version 2

```
+T      count * SC_REC           ; the links, as now, with SC_R_SLOT at +1
+T'     DESK_SC0 bytes           ; the slot byte of zones 0..8 with bit 6
                                 ; set where the user PLACED it, 0xFF where not
+end-4  dw 'SC'  db 2  db count
```

It is written only when there is a link **or a pin**, so a machine whose
user has never touched the desktop still writes the SYSTEM.CFG it always
did. Version 1 has never left the branch: `desktop-shortcuts` is not merged
into `elendilon`, so this needs no migration and the reader keeps a single
version. The record's `SC_R_CAPX` byte goes, because the caption is
measured at paint like a drive's (section 2.3 here), and `SC_R_COL`/`SC_R_ROW` become
`SC_R_SLOT` plus one spare byte. The reader is still `.ovl` and the writer
still CTRL.DRV, so neither costs resident bytes.

---

## 3. What a user or tester will notice

1. **Drives sit 12 px further left on `kern_big`.** A 32 px picture is
   centred in a 96 px cell 4 px off the edge. A second column of drives is
   104 px to the left instead of 44. `kern_small` is unchanged.
2. **Drives stay gapless, as today, around anything PLACED.** A drive the
   user has dragged stays where it was put, and leaves its cell empty while
   it is unmounted, so it can come back to it.
3. **Shortcuts no longer start at the left edge.** They are dropped at the
   nearest free cell anywhere, including the cells drive columns used to
   reserve. Nothing else is reserved.
4. **Drive and Wire highlights** invert the picture and the caption, not the
   32 px band (section 2.3 here).
5. **15 shortcuts, not 16**, so 24 items fit the 24 cells of CGA, EGA and
   Hercules (section 2.1 here).

---

## 4. The bill

ESTIMATED from a sketch, against group A to C's MEASURED 1,260:

| new routine | bytes |
|---|---:|
| `desk_zs` (zone → slot, shown?) | 14 |
| `desk_live` (per kind, for placement only) | 25 |
| `desk_cell_xy` (slot → x, y) | 32 |
| `desk_zone_rect` | 18 |
| `desk_zone_label` (three kinds; a link's staged out of the claim) | 40 |
| `desk_zone_icon` (picture rect by kind, shared by draw and highlight) | 30 |
| `desk_draw_zone` (one fill, three sources, one caption) | 200 |
| `desk_zone_hilite` (picture, badge for a link, caption) | 45 |
| `desk_click` (one loop) | 70 |
| `desk_dmg_zones` (one loop, the mask a dword: `[wm_dmg_zn]` + a word beside it) | 55 |
| `desk_paint` / `desk_paint_mask` | 40 |
| first free / occupancy scan, shared by the reflow and a placed add | 35 |
| `desk_reflow` (section 2.4) | 45 |
| link add/remove into the table | 15 |
| `desk_zones_paint` (dirty cells → one `wm_paint_dmg` per column run) | 50 |
| `osapi_desk_item` (driver half as today, plus the package branch) | 100 |
| **total** | **~810** |

| | `.cold` | `.bss` | net |
|---|---:|---:|---:|
| **kern_big** | −1,260 + ~810 = **~−450** | +24 table, +8 dirty cells, −4 (`desk_zhw`, `desk_zdirty`, `sc_dmgm` folded into the dword) = **+28** | **~−420** |
| **kern_small**: removes B (183 MEASURED) and rewrites C's drive-only half (530 MEASURED) for ~575 | **~−140** | +8 | **~−130** |

CTRL.DRV shrinks as well. `sc_m_cell` (192) and `sc_m_occ` (53) become a
slot loop of about 90 over the resident `desk_cell_xy`, and the caption-pen
measuring goes. That saves image bytes and disk, not resident ones.

**One more lever, not counted above: the Wire's `.bss` mirror (39 bytes).**
It exists because a driver image could vanish. Driver images can also MOVE
now (docs/plans/HEAP-UNPIN-PLAN.md), so a stored seg:off would go stale.
Storing (class, offset) instead and resolving the segment through the class
at use is move-safe. That is about −35 net, and the caption already goes
through the link's stager. It is a measured A/B for wave 4, not part of the
estimate.

**How sure is ~−420?** The removals are measured to the byte. The additions
are a count of a sketch, and sketches in this tree have run 10-30% low.

| additions run | `.cold` | net with `.bss` |
|---|---:|---:|
| as sketched (810) | −450 | **−422** |
| 10% over (891) | −369 | −341 |
| 30% over (1,053) | −207 | −179 |

So 400 is reachable but not certain. It depends on the new
`desk_draw_zone` and `osapi_desk_item` coming in near their estimates, and
the 39-byte mirror lever above is the reserve. Each wave reports its own
`kernsize` delta, so a shortfall shows at wave 1 and not at the end.

---

## 5. The waves

Each wave builds, is measured with `tools/kernsize.py` against the wave
before, and is looked at on VGA, the CGA and Hercules (§1's rule about
`[vid_*]`, and a drive-zone change is not done until it has been seen on
the CGA).

1. **The grid and the table, for drives and the Wire.** `desk_zslot`,
   `desk_cell_xy`, `desk_reflow` and the deferred boot
   placement, the dirty-cell set, and one `desk_draw_zone`/hilite/click
   /damage over zones 0..8. Group B goes. On `kern_small` this is the whole
   change. Gates: `deskfdd`, `wirezone`, `hdboot`, `small128`, `deskbench`,
   and `tools/os88geom.py`'s `drive_xy`, which reads the table instead of
   recomputing ordinals.
2. **Links onto the same table.** Group A goes, `DESK_SC0` becomes 9,
   `SC_R_SLOT`, and CTRL.DRV's cell search becomes a slot search.
   Gate: `desksc`.
3. **Every item draggable, and the pins persisted.** `desk_click`'s
   `cmp al, DESK_SC0` arm goes, CTRL.DRV's move takes any zone, and trailer
   version 2. Gate: `desksc` extended with a drive dragged and a reboot.
4. **`OSAPI_DESK_ITEM`.** The record, the package branch, the SDK macros
   and `os88.h`, the Wire drivers' record grows its slot byte, and a
   `tests/` package that adds and removes a link. Then the (class, offset)
   A/B.
5. **SPEC.md.** §26.1, §26.3, §26.7 and §26.8 rewritten as one section, with
   this file moved to `completed/` in the same commit, and docs/INDEX.md
   regenerated.

---

## 6. Decisions

The owner answered on 2026-10-01:

| # | question | DECIDED |
|---|---|---|
| D1 | one cell size moves `kern_big`'s drives 12 px left | build it and look. **Fallback if the look fails:** a narrower cell, with captions cut to an 8-character stem, and possibly a short display name a package can store for its shortcut |
| D2 | holes or gapless | **gapless**: re-pack every unplaced drive on any change, knowing only that they are drives (section 2.4). It stays resident, because volumes change outside boot and the Control Panel |
| D3 | 25 items, 24 cells | `SC_MAX` 15, so the item limit equals the cells |
| D4 | moving an icon needs the system disk | accepted: it is a setting like any other, and a setting needs the system disk to persist |
| D5 | `kern_small` | takes the same code if it is negative or under about +100 bytes; otherwise gated behind `%ifdef` |
| D6 | the trailer format | free to change; the feature has shipped to no one |

Still open, decided by default unless the owner says otherwise:

| # | question | default |
|---|---|---|
| U3 | may a package remove a link it did not add? | yes: there is no owner identity to check, and the user can make it again |
| U4 | should `Remove` on a drive mean unmount? | no: removal stays for links, and unmounting stays in the Control Panel |

---

## 7. What it came to

MEASURED with `tools/kernsize.py` against `9a760ab`, at one commit each:

| | kern_big | kern_small |
|---|---:|---:|
| `.text` | −16 | 0 |
| `.bss` | +28 | +15 |
| `.cold` | −300 | −5 |
| **resident** | **−288** | **+10** |
| `.ovl` / `.ovlw` | +14 / +17 | +5 / +17 |
| `CTRL.DRV` | +103 | — |

**kern_big came in 132 short of the ~−420 estimate, and inside section 4's
"10 to 30% over" band, at about 20% over.** Two things were added after
the estimate and are named rather than absorbed. A package's half of
`OSAPI_DESK_ITEM` sends a 128-byte link through `CTRL.DRV`. A placed drive
REMEMBERS its cell across an unmount (section 3, item 2, built as written).
The 39-byte mirror lever was not taken: the right-click work below wanted the
bytes more.

**kern_small came in at +10, not ~−130.** Its one-loop reflow and table
replaced the ordinal arithmetic nearly byte for byte, so the estimate's
removal of group B was offset by the table's `.bss`. That is well inside D5's
line, so the code is shared and not gated.

Six things the build found that this plan did not have:

1. **The right-click popup went into `CTRL.DRV` for a cycle, and that was
   wrong.** Every gesture already loaded the image for its SYSTEM.CFG write,
   so moving the popup too looked free. But the popup has no write until the
   pick, and on an XT the load is 2–3 seconds between the click and the
   menu. The owner caught it on the glass. The popup is resident again and
   only the pick loads the module (SPEC.md 26.8.5).
2. **The `desktop-shortcuts-optimization` branch had already done a size
   pass on the PRE-unification shortcuts, and most of it still applied.**
   It contributed:
   - one record whose +62 is the icon record's header, so a link's picture
     draws straight out of the claim;
   - the path stored below the root;
   - `sc_open` as one body with the volume and kind in BP;
   - the caption rect measured with `font_width` at paint, not stored;
   - the repaint per CELL rather than per column run. The dirty set already
     names cells, so section 2.4's run-merging was code for no saving.
3. **`desk_live`'s `sbb al, al / inc ax` read ZF off a word.** With AH
   non-zero, a dead link zone answered LIVE, so the first reflow gave all 15
   empty link zones a cell each. Nothing drew them, because the draw asks
   `sc_row` itself. They showed up only when a package asked for cell 5 and
   was given the first free cell, because 2..16 were all "taken".
   `tests/deskitem.py` caught it. The fix is `inc al`.
4. **The first build of the placed-item rule forgot placed drives at boot.**
   The trailer is read before `drv_boot` adds the hard disk's volumes, so a
   reflow in between saw a placed C: that was not live yet and dropped its
   cell. `[desk_ready]` holds every reflow until kmain has added all the
   volumes (SPEC.md 26.9.3).

5. **The grid left visible slots bare on Hercules**, which the owner saw
   on the glass. There were two causes. Rows were counted as whole pitches
   above the dock, though the last row needs only its own 46 rows. And a
   104 pitch put the leftmost of six columns at x = 100. The fixes are a
   row count of `(dock − 32 − zh1 − 1) / pitch + 1` and a 102 pitch, so
   Hercules went from 24 cells to 35, VGA and the CGA are unchanged, and the
   cost is −1 byte (SPEC.md 26.9.1). The one-byte saving then STALLED
   kern_small's assembly. `mod.inc` compares against `MOD_STAMP`, which is
   the sum of the section sizes that compare sits in, and a stamp of
   0xFF80..0xFFFF has no fixed point. `strict word` ends it, for nothing.
6. **On the CGA, a link's picture was drawn under its own caption.** Nothing
   wrote `[desk_lky]` after the port (+9 bytes).

U3 and U4 shipped as their defaults. Section 2.4's run-merging repaint and
section 4's mirror lever were not built.

Gates: `desksc` (now also a drive dragged and a reboot), `deskitem` (new: a
package adds a link at cell 5 and removes it), `deskfdd`, `wirezone`,
`thewire`, `uilayer`, `hdnoclaim`, `small128` and the fast tier, all green
on the final tree.
