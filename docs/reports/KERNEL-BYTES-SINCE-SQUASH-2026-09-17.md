# Kernel bytes since the last squash with `main`, by concept, with `main`'s arm separated

**A measurement, not a description.** Taken 2026-09-17 on a four-core cloud
container from a cold checkout — `nasm` 2.16.01, QEMU 8.2.2, MartyPC built at
the pinned commit. Every figure comes from `tools/kernsize.py --json` and its
`--bless` module table, which RE-ASSEMBLE the kernel rather than reading a
build, so each point is measured on its own tree with its own tools and no
figure is carried between them. It is true of the six commits it names and of
no other tree.

**RE-MEASURED 2026-09-18, and the earlier figures are unedited.** The branch
kept moving after this file was written — 124 commits, of which the first
seven are the size passes its own audit at the foot asked for — so a sixth
point **F** is added below and every table carries a new column for it.
Nothing that was measured on 2026-09-17 has been rewritten: **D was
re-assembled from a clean worktree on 2026-09-18 and reproduces to the byte on
both kernels** (`.text` 49,891, `.bss` 6,092, `.cold` 40,899, `KERN_SIZE`
111,616 big; 37,263 / 4,179 / 26,588 / 75,264 small), which is what makes A→D
and D→F comparable at all. The method is unchanged and so is the tool. This is
an EXTENSION of a measurement, not a second one: a genuinely new measurement —
a different box, a different `nasm`, a different question — is still a new
file.

It is the THIRD of its family and the family has two names.
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-09-07.md` is the first and
`docs/reports/PR-CYCLE-ACCOUNTING-2026-09-11.md` the second — the same
three-point split under a different title, taken against the #172 squash.
Nothing here is an edit of either.

## The points

| | commit | what it is |
|---|---|---|
| **A** | `2237d1b` | **the last squash with `main`** — *Elendilon -> Main (CLEAR SKIES performance, DOT DELIRIUM, GFX Lines Library per app, Office/Games/Network 360k disks)* (#179), 2026-09-13 |
| **B** | `0ba4677` | `elendilon-next` at its tip before this session's merge, **+562 commits** over A |
| **C** | `dfd5796` | `main` at its tip, **+9 commits** over A |
| **D** | `ba34ab5` | the merge of B and C **plus the five size passes of 2026-09-17** |
| **E** | `5f20918` | an intermediate point on B's arm, used only to split one module — see *Two lanes* |
| **F** | `bde3343` | **the tip as this file is updated**, 2026-09-18 — D **+124 commits**: the seven size passes the audit below asked for, the button control, and the soak lane's fixes |

**A→B is the branch's own arm, A→C is everything `main` did, A→F is what the
tree carries now** — A→D being what it carried on 2026-09-17. B and C are
siblings, not ancestors: `main` squash-merges, so A is where the two last
agreed.

**The two arms overlap by exactly one commit and it is worth nothing.**
`git merge-base origin/main 0ba4677` is `8d0f363` (#180), which B already
carries, so A→B and A→C both contain it. Its whole diff is one file —
`.claude/skills/release-os8088/mkzip.py`, 1 insertion and 2 deletions — and no
kernel byte moves in it. Every other commit is on one arm or the other.

**The build number contributes nothing to any delta here.** `BUILD_STR` is the
commit count as a decimal string (SPEC.md 14.2) and the five counts are 153,
715, 162, 743 and **867** — three digits at every point, so the About box's
string is three bytes at every point. A comparison that crossed 999→1000 would
not be able to say this, and the next one of these reports will not be able to:
F is 133 commits from the fourth digit.

**`KERN_BUDGET` is 129,536 at all five points and `KERN_SMALL_BUDGET` 107,520.**
Nothing below is a budget move; every figure is a size move.

## Headline — `kern_big`, the shipped default

| section | A base | B ours | C main | D 09-17 | **F now** | **B−A** | **C−A** | **F−A** |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `.text` | 49,539 | 49,416 | 50,146 | 49,891 | 49,982 | **−123** | **+607** | **+443** |
| `.bss` | 6,016 | 6,006 | 6,151 | 6,092 | 6,106 | **−10** | **+135** | **+90** |
| `.cold` | 39,265 | 41,166 | 39,356 | 40,899 | 40,669 | **+1,901** | **+91** | **+1,404** |
| `.ovl` | 1,417 | 1,417 | 1,588 | 1,511 | 1,511 | 0 | **+171** | **+94** |
| `.ovlw` | 5,052 | 5,084 | 5,052 | 5,084 | 5,084 | +32 | 0 | +32 |
| `.lowbss` | 9,182 | 7,966 | 9,182 | 7,966 | 7,966 | **−1,216** | 0 | **−1,216** |
| `.vgabuf` | 848 | 848 | 848 | 848 | 848 | 0 | 0 | 0 |
| **sum** | | | | | | **+584** | **+1,004** | **+847** |
| **`KERN_SIZE`** | 110,592 | 111,616 | 111,104 | 111,616 | 111,616 | **+1,024** | **+512** | **+1,024** |
| spare of `KERN_BUDGET` | 18,944 | 17,920 | 18,432 | 17,920 | 17,920 | | | |

Three rungs were spent between the two arms and **one has been given back**:
B−A is two rungs, C−A is one, and F−A is two rather than three. That last 512
is the five size passes of 2026-09-17, and it is the only reason the merged
tree is not three rungs above the squash.

**D→F gave back a further 125 bytes of section and crossed no rung**, which is
the shape the banner in CLAUDE.md asks for and not a disappointment: `.cold`
−230 against `.text` +91 and `.bss` +14. The passes and the features in that window very
nearly cancel, and the section that gave is the one that costs the DOS arena
byte for byte. Of the A→F total, **+721 is resident** (`.text`, `.bss`, `.cold`,
`.lowbss`) and +126 is overlay that the machine reuses once it is up.

## Headline — `kern_small`, the 128KB floor machine

| section | A base | B ours | C main | D 09-17 | **F now** | **B−A** | **C−A** | **F−A** |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `.text` | 37,453 | 37,278 | 37,465 | 37,263 | 37,303 | **−175** | **+12** | **−150** |
| `.bss` | 4,242 | 4,179 | 4,242 | 4,179 | 4,193 | **−63** | 0 | **−49** |
| `.cold` | 26,197 | 26,731 | 26,197 | 26,588 | 26,442 | **+534** | 0 | **+245** |
| `.ovl` | 423 | 423 | 423 | 423 | 1,333 | 0 | 0 | **+910** |
| `.ovlw` | 2,789 | 2,820 | 2,789 | 2,820 | 1,910 | +31 | 0 | **−879** |
| `.lowbss` | 5,460 | 5,236 | 5,460 | 5,236 | 4,436 | **−224** | 0 | **−1,024** |
| **sum** | | | | | | **+103** | **+12** | **−947** |
| **`KERN_SIZE`** | 75,776 | 75,776 | 75,776 | 75,264 | 74,752 | **0** | **0** | **−1,024** |
| spare of `KERN_SMALL_BUDGET` | 31,744 | 31,744 | 31,744 | 32,256 | 32,768 | | | |

**The floor machine is a full KILOBYTE smaller than it was at the squash** —
two 512-byte rungs — after a cycle that added a DOS box, an icon store and a
document-glyph column to the tree. It was 512 on 2026-09-17 and the second rung
came the day after. It is the only line in this document that is unambiguously
good news, and it is worth saying why it is not luck: `kern_small` gets none of
`main`'s two big features (`DOCK_OPT` is inside `%ifdef KERN_BIG`), the
branch's own arm spent only 103 bytes of section there against `kern_big`'s
584, and the icon store's pass on 2026-09-17 took a whole `.cold` rung back.

**The `.ovl` +910 / `.ovlw` −879 pair is ONE move and not two, and it is the
second rung.** `3884e6be` sends `mouse.inc`'s serial probe (648 bytes) and
`vidsel.inc`'s adapter probe (262) to `.ovl` on `kern_small` and leaves them in
`.ovlw` on `kern_big`, which takes `.ovlw` 2,820 → 1,910 and so 3,072 → 2,048
once rounded to whole sectors. The overlay net is **+31, exactly what it was at
B**; what the move BUYS is underneath it — `.ovlw` had occupied 3,072 of the
listing region's 3,136, so the listing was not free to shrink, and freeing it
let `dskwin.inc` shed **800 bytes of `.lowbss`**. That is the row that matters:
`.lowbss` sits below `HEAP_SEG`, so those 800 are heap on every 128KB machine.
It cost 8 resident bytes of `.text`. **Resident A→F is −978** on this kernel;
the overlay +31 is the rest of the −947.

**`main`'s twelve bytes are the second-most-useful line here.** `main`'s nine
commits cost the floor machine **12 bytes of `.text` and not one byte of
footprint** — the `kern_small` refusal stub for `OSAPI_MOUSE_FEED` (SPEC.md
20.8 rule 4) and nothing else. The previous report in this series found the same
shape and it has held for a second cycle.

## `main`'s arm, by concept

Four of `main`'s nine commits touch kernel bytes. The module deltas are
A→C, exact from `kernsize --modules`.

| concept | PRs | modules | `.text` | `.cold` | `.bss` | total |
|---|---|---|---:|---:|---:|---:|
| **the Dock — placement, auto-hide, the Control Panel page** (SPEC.md 30.5–30.6) | #189, #193 | `dock.inc` +327, `wm.inc` +78, `ui.inc` +30, `menu.inc` +22, `ctrl.inc` +15, `mod.inc` +13, `vidsel.inc` +9, `fsx.inc` +3 | +462 | +40 | +69 | **+571** |
| **the USB mouse on the CH375** (SPEC.md 9.12) | #187 | `mouse.inc` +92, `driver.inc` +54, `kernel.asm` +24 | +145 | +9 | +39 | **+193** |
| **the file dialog's size column** | #188 | `fdlg.inc` +42 | 0 | +42 | 0 | **+42** |
| incidental reductions | — | `font.inc` −8, `icons.inc` −2, `viddet.inc` −1 | −11 | 0 | 0 | **−11** |

The Dock figure is an A/B rather than an attribution: built with `DOCK_OPT`
defined and undefined on the same tree, which is why it carries `.ovl` +166 that
no module row shows. **The Dock is exactly the 512-byte rung `kern_big` crossed
on `main`'s arm**, and `.ovl` +166 on top of it.

## Our arm, by concept

Our arm is 562 commits to B and a further 124 from D to F — 155 of which touch
`kernel/`, `boot/`, `kerndos/` or `apps/os88api.inc`. Attribution below is **per module**, which `kernsize` gives
exactly, with the concept named from the commits that touched it. Where a module
carries more than one concept it is split at a measured intermediate point, not
apportioned by source lines — a line is not a byte and this project refuses that
arithmetic.

### Three lanes

One module, `kernel/disk.inc`, carries two unrelated concepts and is +909 code
across the window to B. It is split at **E = `5f20918`** (*The read-ahead asks
what the machine HAS as well*), the last commit of the DOS/read-ahead work and
the one before the icon store's first: A→E is the first lane, E→B the second.
**D→F is the third**, and it needs no splitting — it is the merge's own
follow-on, and `kern_big` figures unless a row says otherwise.

| lane | `.text` | `.cold` | `.bss` | `.lowbss` | code |
|---|---:|---:|---:|---:|---:|
| **A→E** the DOS box's kernel surface, the read-ahead, the compactor | −216 | +855 | −24 | 0 | **+639** |
| **E→B** the icon store, the document glyph, the late work | +93 | +1,046 | +14 | −1,216 | **+1,139** |
| **D→F** the passes this file asked for, the button control, the soak lane | +91 | −230 | +14 | 0 | **−139** |

### Lane 1 — what the DOS box asked the kernel for (A→E, +639)

The DOS box itself is `kerndos/` and `apps/dos/` — a package and a module
image, not resident kernel. What it cost the KERNEL is the doors it needed.

| concept | modules | code |
|---|---|---:|
| the heap's purge floor, self-compaction and `mem_regrow`'s shed (SPEC.md 50.6.6, 66.4.3) | `memory.inc` | **+193** |
| the read-ahead window: claims no DMA page, moves, asks what the machine has (SPEC.md 18.95.7, 50.6.7) plus the DOS path walkers | `disk.inc` | **+255** |
| `OSAPI_FILE_COPY` / `OSAPI_FILE_MOVE`, one door with a verb (SPEC.md 22.24–22.25) | `filecp.inc` | **+126** |
| `OSAPI_DRV_SUSPEND` and the DOS handoff as its verb 2 (SPEC.md 51.11, 96.40) | `hiber.inc` +71, `driver.inc` +33 | **+104** |
| `OSAPI_FILE_WRITE_AT` / `_APPEND` and `OSAPI_VOL_STAT` (SPEC.md 18.4.6, 18.4.7) | `diskw.inc` (`.text` −97, `.cold` +134) | **+37** |
| the file dialog, `OSAPI_FILE_PATH`'s walkers, the rest | `fdlg.inc` +39, `files.inc` +20, `wm.inc` +28, `ui.inc` +12, `snd.inc` +7, `vga12.inc` +5, `loader.inc` −3, `mouse.inc` −18 | **+90** |

### Lane 2 — the icon store and what came after (E→B, +1,139, −1,216 of `.lowbss`)

| concept | modules | code | `.lowbss` |
|---|---|---:|---:|
| the listing's **icon store**: one body per machine, a pool, purgeable (SPEC.md 25.8) | `disk.inc` +654, `dskwin.inc` | **+654** | **−1,216** |
| a package may **ship its 8x8 document glyph** (SPEC.md 54.3.2) | `assoc.inc` | **+197** | 0 |
| `ld_pkg_byname` — the DOS shell's `open <document>` arm (SPEC.md 21.5.3) | `loader.inc` | **+159** | 0 |
| **Alt+Enter** into and out of full screen (SPEC.md 96.33.5.1, 9.7.1) and the mouse work beside it | `mouse.inc` +42, `ui.inc` +25, `fsx.inc` +5 | **+72** | 0 |
| the DOS box's Memory page: a suspend mask and a class figure (SPEC.md 51.12) | `driver.inc` +29, `memory.inc` +10, `files.inc` +18 | **+57** | 0 |

**The `.lowbss` line is the one to read twice.** `.lowbss` sits BELOW `HEAP_SEG`
in the ladder, so every byte there comes off the heap and therefore off the DOS
arena byte for byte. The icon store spent 654 bytes of `.cold` to take 1,216 off
`.lowbss`, and `docs/reports/DOS-GAMES-2026-09-16.md` has Chip 'n Dale losing by
three kilobytes — which is what those 1,216 bytes are for.

### Lane 3 — what this file's own audit produced (D→F, −139, and a rung off `kern_small`)

**This lane exists because the audit at the foot of this file was written and
then acted on within the day.** Seven of the first commits after it are size
passes aimed at rows it marks NO or PARTIAL, which is why a 124-commit window
carrying a new API slot and six behaviour fixes still comes out NEGATIVE.

The module deltas are exact, D→F, from `kernsize --modules` on both trees:

| module | `.text` | `.cold` | `.bss` | code | what moved it |
|---|---:|---:|---:|---:|---|
| `instance.inc` | −73 | −74 | 0 | **−147** | the re-home's sweep, `inst_next` (SPEC.md 29.1.1) |
| `disk.inc` | 0 | −138 | −6 | **−138** | the read-ahead asking one question once (18.95.9) |
| `loader.inc` | 0 | −123 | 0 | **−123** | the re-home, and `OSAPI_PKG_START`'s by-name arm (21.5.3.2) |
| `fdlg.inc` | +4 | +116 | +20 | **+120** | a −138 size pass, then the button records on top of it |
| `wm.inc` | +91 | 0 | 0 | **+91** | the click slot, the obstruction test, the vertical cut |
| `viddet.inc` | +22 | 0 | 0 | **+22** | the dual-screen CGA mode byte (39.18.1.1, 39.19.4.1) |
| `font.inc` | +21 | 0 | 0 | **+21** | `font_char`'s vertical cut (11.3.4) |
| `ui.inc` | +16 | 0 | 0 | **+16** | a resize that changed nothing vacates nothing (11.91.5) |
| `files.inc` | −5 | −8 | 0 | **−13** | the walker conversion |
| `kernel.asm` | +8 | 0 | 0 | **+8** | `OSAPI_WM_ONCLICK`'s cell (0x05A0) |
| `blank.inc` | +6 | 0 | 0 | **+6** | a saver session darks every monitor but its own (79.1.1) |
| `vidsel.inc` | −4 | 0 | 0 | **−4** | — |
| `assoc.inc` | 0 | −3 | 0 | **−3** | the case fold leaving the kernel |
| `fsx.inc` | +3 | 0 | 0 | **+3** | — |
| `menu.inc` | +2 | 0 | 0 | **+2** | `menu_hover` reads the pointer once |
| **total** | **+91** | **−230** | **+14** | **−139** | |

**The passes, each with the commit's own bracket** — taken against D, which is
why they do not sum to the module column where one module got a pass AND a
feature:

| pass | commit | `kern_big` | `kern_small` |
|---|---|---:|---:|
| the re-home's 174 bytes back, from six shared bodies | `2ba7934f` | −202 | −178 |
| the five remaining `inst_tab` walkers | `a68d953a` | −13 | 0 — all three conversions are inside `%ifndef KERN_SMALL` |
| the read-ahead cache asked the same question four times | `db992657` | −144 | −144 |
| `fdlg.inc`'s own pass | `2644f040` | −138 | `FDLG.DRV`'s image −137 |
| `OSAPI_PKG_START`'s document arm, 133 → 65 | `c17478f8` | −71 | −11 |
| `kern_small`'s boot-overlay split | `3884e6be` | +0 | **−800 of `.lowbss`, −512 of `KERN_SIZE`** |

**`fdlg.inc` is the row to read twice, and it is not a failure.** The module
took a −138 pass and came out +120, because the button conversion
(SPEC.md 20.5.1.2) then put `OS88UI_BTNREC` records into it. Both are true and
the module is what it is; what would have been dishonest is quoting the −138
alone. The pass's own finding is worth more than its bytes: `kernsize
--modules` brackets a file's `%include`s, and `fdlg.inc` has `os88ui.inc`
nested inside it, so **~1,600 bytes billed to the dialog are the shared
control's and are not the dialog's to cut** — a 50% cut of the reported figure
would have been 72% of the real one.

**What the lane ADDED, each measured by the commit that added it:**

| concept | § | `kern_big` | `kern_small` |
|---|---|---:|---:|
| `OSAPI_WM_ONCLICK` (0x05A0) — the press belongs to the button library, so no click path can skip it | 20.5.1.2 | +8 (five bytes of body and one cell) | +16 |
| chrome that is WHOLLY obstructed is not drawn — the 1bpp CGA title strip reads 1,434 → 0 | 11.97.3, 11.97.4 | +36 | +36 |
| the vertical cut: `font_char` draws a cell the region cuts vertically | 11.3.4 | +93 | **0 — byte-for-byte identical**, its image rung having 73 bytes left where the cut wants 78 |
| a resize that changed NOTHING vacates nothing | 11.91.5 | +16 | +16 |
| the dual-screen Herc/CGA mode byte, and `vid_text`'s missing `vid_cga_equip` | 39.18.1.1, 39.19.4.1 | +9 | +6 |
| a saver session darks every monitor but its own | 79.1.1 | +6 | **0 — byte-identical**, that build having no second display |

### The size passes already inside this window

Our arm is **net negative in `.text` on both kernels** (−123 big, −175 small)
while adding every concept above, and that is not an accident. The
`size-pass-kernel-additions` branch ran inside this window and its record is
`docs/reports/GFXBENCH-SIZE-PASS-2026-09-16.md`: **1,820 resident bytes**,
taking `kernel.asm` −111 and `fprog.inc` −55 among others, and retiring two
cells off the API table's tail. The five passes of 2026-09-17 took a further
616 bytes of section and one 512-byte rung on each kernel, and the six of
2026-09-17/18 in lane 3 took **568 more of `kern_big` and a second rung off
`kern_small`**.

## What each arm cost, in one line each

- **`main`** spent **+1,004 bytes of section and one rung** on `kern_big` for
  three concepts, of which the Dock is 571 and `.ovl` +166 on top. On
  `kern_small` it spent **12 bytes and no rung**.
- **We** spent **+584 bytes of section and two rungs** on `kern_big` to B for a
  DOS box's worth of kernel surface, an icon store and a glyph column — while
  giving back 1,216 bytes of `.lowbss` and taking `.text` DOWN on both kernels.
  On `kern_small`, **+103 bytes and no rung**. The 124 commits from D to F then
  took **139 bytes of `kern_big` back** and a second rung off `kern_small`,
  while adding an API slot and six behaviour fixes.
- **Together, at F**, the tree stands **+847 bytes of section and two rungs**
  above the squash on `kern_big` — of which +721 is resident and +126 is
  overlay the machine reuses — and **a full kilobyte and two rungs BELOW it**
  on `kern_small`.

## Where that leaves the two kernels

| | `kern_big` | `kern_small` |
|---|---:|---:|
| `KERN_SIZE` | 111,616 | **74,752** |
| budget | 129,536 | 107,520 |
| **spare** | **17,920** (35 steps of 512) | **32,768** (64 steps) |
| `.text`+`.bss` of `KERN_CODE_MAX` | 56,088 of 65,536 — **9,448 left** | 41,496 — 24,040 left |

`KERN_CODE_MAX` cannot be raised at all (offsets are 16 bits) and is the
constraint to watch: 9,448 bytes against `KERN_BUDGET`'s 35 steps. `main`'s arm
spent 742 of those 9,448 and ours took 28 back — 133 at D, of which lane 3 then
spent 105 on the vertical cut and the obstruction test. **That is the number to
watch and not the rungs**: `KERN_CODE_MAX` has moved 105 the wrong way in a
window that moved `KERN_SIZE` not at all, which is exactly the reading the
banner in CLAUDE.md exists to force.

## Which of these concepts has had a size optimization pass

A second question, answered off the same accounting: for the last TWO squash
cycles, has each kernel-byte-touching concept been through a pass of its own?
"Yes" means a commit or a branch that took bytes out of THAT concept after it
landed — not a measurement of it, and not a size-conscious choice made while
writing it.

### This cycle (A→F, since #179) — one outstanding, and six too new to have had one

**Four of the five rows this table marked NO or PARTIAL on 2026-09-17 were
passed within the day**, which is lane 3. They are updated in place below with
the commit that reached them; the one that was not is the mouse wire work, and
it is the one row here that has now stood through two audits.

| concept | arm | code | pass |
|---|---|---:|---|
| the Dock (SPEC.md 30.5–30.6) | main | +571 | **yes** — 2026-09-17, 571 → 486 and the router 227 → 42 guest cycles |
| the USB mouse / CH375 (9.12) | main | +193 | **yes** — 2026-09-17, 145 → 94 `.text` |
| the file dialog's size column (#188) | main | +42 | **yes** — `2644f040`, 2026-09-17: `fdlg.inc` −138, which is the module and so reaches this row and ours below together |
| the purge floor, self-compaction, `mem_regrow`'s shed (50.6.6, 66.4.3) | ours | +193 | **yes** — `e820ed46`, `212f3293`, `bfe562bf`, `e7d97d7a` |
| `OSAPI_FILE_COPY` / `_MOVE` (22.24–22.25) | ours | +126 | **yes** — `c54f7b0a`, −215 bytes |
| `OSAPI_DRV_SUSPEND` + the DOS handoff (51.11, 96.40) | ours | +104 | **yes** — `9b3edc6a` |
| `OSAPI_FILE_WRITE_AT` / `_APPEND`, `OSAPI_VOL_STAT` (18.4.6–18.4.7) | ours | +37 | **yes** — `eb4e7de6`, `a2ef1b6d` |
| `OSAPI_FILE_PATH` (19.2.4) | ours | — | **yes** — `e99265a1`, 393 → 137 |
| the API table's tail and its cell thunks (20.3.1–20.3.2) | ours | −111 | **yes** — `8ffb6b98`, `2985908d` |
| the busy pointer's clock, the scroll bars' release (7.5, 13.10.5.4.2) | ours | — | **yes** — `da8adbff` |
| the icon store (25.8) | ours | +654 | **yes** — 2026-09-17, −303 `.cold` and a rung on both kernels |
| the shipped document glyph (54.3.2) | ours | +197 | **yes** — 2026-09-17, same pass |
| Alt+Enter (96.33.5.1, 9.7.1) | ours | +72 | **yes** — 2026-09-17, 72 → 46 `.text` |
| the Memory page's two slots (51.12) | ours | +57 | **yes** — 2026-09-17, 111 → 52 |
| the read-ahead cache — claims no DMA page, moves, the ladder, the width command (18.95.7, 50.6.7) | ours | part of +255 | **yes** — `db992657`, 2026-09-17: −144 on both kernels, its surface 651 → 530, new section 18.95.9 |
| `ld_pkg_byname` / `ld_pkg_upc` — the DOS shell's `open <document>` arm (21.5.3) | ours | +159 | **yes** — `c17478f8`, 2026-09-17: 133 → 65 resident, the case fold moved to the caller (21.5.3.2) |
| **the mouse wire work — a wheel mouse's fourth byte, `MOU_IDMAX` 8 → 128, `MOU_DRAINT`, `MOUROUND`** | ours | **part of `mouse.inc`** | **NO — and this is its second audit.** `mouse.inc` is the one module untouched by lane 3: net 0 across D→F |
| `fdlg.inc` +39, ours | ours | +39 | **yes** — `2644f040`, same pass as #188's row above |
| **`OSAPI_WM_ONCLICK` and the button as a control (20.5.1.2)** | ours | **+8 big, +16 small** | **too new** — landed in lane 3 |
| **chrome wholly obstructed (11.97.3, 11.97.4)** | ours | **+36** | **too new** |
| **`font_char`'s vertical cut (11.3.4)** | ours | **+93 big, 0 small** | **too new** |
| **a resize that changed nothing (11.91.5)** | ours | **+16** | **too new** |
| **the dual-screen CGA mode byte (39.18.1.1, 39.19.4.1)** | ours | **+9 big, +6 small** | **too new** |
| **the saver's second monitor (79.1.1)** | ours | **+6 big, 0 small** | **too new** |

**"Too new" is a status and not an excuse, and the pattern at the foot of this
file says what it predicts**: a cycle's additions get audited and then passed
one cycle late. Those six are 168 bytes of `kern_big` between them — the size
of one of the rows the passes above just reached — and the next audit is where
they are owed a column, not this one.

### The previous cycle (what #179 carried, #172 → #179) — NOTHING outstanding

**`main` added no kernel bytes of its own in that window at all.** Of the
sixteen commits on `main` between #172 and #179, only #179 itself — our squash
— touches `kernel/`, `boot/` or `apps/os88api.inc`. Its other six (#161, #162,
#171, #176, #177, #178) are packages and release tooling. So every kernel byte
in that cycle is ours, and the concepts are the ones its diffstat names:

Measured the same way as everything else above — `P` = `2ce1e37`, the #172
squash, against `A` = `2237d1b`, the #179 one, both re-assembled. **In BYTES,
not in diffstat lines, and the two disagree violently**: `ctrl.inc` is +451
LINES in #179's diffstat and **+1 byte** in the kernel, because the Control
Panel's body is `CTRL.DRV`, an on-demand module. The first draft of this
report quoted the line counts and they are not a proxy for anything.

| concept | `kern_big` bytes | pass |
|---|---:|---|
| the GFX lines library moved into the apps that use it (`vga12.inc`) | **−1,690** | **n/a — it IS the reduction** (`docs/plans/completed/GFX-EMBEDDABLE-PLAN.md`) |
| the file dialog (`fdlg.inc`, `.text` −146 / `.cold` +45) | **−101** | n/a — net negative |
| the busy cursor, hourglass then clock (`mouse.inc`) | **+233** | **yes, one cycle late** — measured by `docs/reports/BUSY-CURSOR-COST-2026-09-10.md`, then passed by this cycle's `da8adbff` |
| the package re-home and `.o88` parts (`loader.inc` +174, `instance.inc` +25) | **+199** | **yes** — `2ba7934f`, 2026-09-17: −202 `kern_big` / −178 `kern_small`, and the re-home arm itself 51 smaller with a check ADDED. The `instance.inc` +25 had already come back |
| nothing permanently pinned on the heap (`memory.inc`) | **+62** | **partially** — reached by this cycle's `e820ed46` / `212f3293` |
| the API table and the shims (`kernel.asm`) | **+42** | **yes, one cycle late** — this cycle's `8ffb6b98`, `2985908d` took 111 back |
| the rest (`wm.inc` +10, `fprog.inc` +6, `ctrl.inc` +1) | **+17** | NO, and not worth one |
| **the cycle, whole** | **−1,238** | |

**So the previous cycle SHRANK the kernel by 1,238 bytes** and the alarming
part of "it was never size-passed" evaporates on contact with the measurement:
one concept in it added more than 200 bytes, and that one has since been
passed. **Nothing from that cycle is outstanding any more**: the re-home and
parts work, the last row at 199 bytes, was passed by `2ba7934f` for more than
it cost.

**The evidence that no pass ran in that cycle is its own subject list.** #179
carries 326 subjects and exactly one of them is size work on the kernel's own
additions — *"The PR cycle's byte audit - and the package it found nothing was
building"*, which is `docs/reports/PR-CYCLE-ACCOUNTING-2026-09-11.md` and is an
ACCOUNTING, not an optimisation. #172's subject list has none at all. Kernel
size passes 1 to 4 all landed by #147, two squashes earlier
(`docs/plans/completed/HANDOFF-KERNEL-SIZE-P3.md`).

So the pattern across two cycles is that **a cycle's kernel additions get
audited and then passed one cycle LATE** — this cycle's
`size-pass-kernel-additions` is what reached the previous cycle's busy cursor,
its API table and its heap work, and the 2026-09-17 passes reached this cycle's
four biggest. That is a working arrangement rather than a failure, and the
measurement says so: the previous cycle is 1,238 bytes DOWN with **nothing**
outstanding.

**And this update is the arrangement running a cycle EARLY for once.** The
table above was written on 2026-09-17 naming four rows NO or PARTIAL; lane 3
reached all four, plus the previous cycle's last one, inside the same window —
so the lateness this section describes is a tendency and not a law, and what
broke it was writing the list down. What has never been reached by anything is
**one row**: the mouse wire work. It is named at the top of the table and it is
the thing to take next.
