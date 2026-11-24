# Desktop wallpaper - the plan (SPEC.md 136)

The owner's brief, 2026-11-23:

> add Desktop wallpaper (Windows 3.1 wallpaper, OS/2's background bitmap). It
> would only be acceptable if it redraws just the uncovered areas, a tiled
> 4bpp picture plus a 1bpp version. Otherwise it becomes the full-screen
> repaint that PERFORMANCE.md Part 5 counts as a regression. Use codex image
> gen cli to generate the design for the vga/vesa control panel and for the
> regular control panel.

Contract: SPEC.md 136. Diary: `docs/HOW-WALLPAPER-WAS-BUILT.md`. Design
reference: `docs/wallpaper/` (mockups and their prompt), `kernel/wallart/`
(tile sources and their prompt).

Built with `/story-fanout`: one agent per file-disjoint story, merged into
`feature/wallpaper` as each finishes, then ONE QA pass in emulation, a fix
round, and the final pass once.

Status letters: `-` not started, `B` building, `I` integrated into
`feature/wallpaper`, `S` seen working in the QA pass, `X` deferred.

## Stories

| id | agent | status | story | done when |
|---|---|---|---|---|
| W0 | lead | I | The contract (SPEC 136), this plan, the mockups and tile art kept as source | committed on `feature/wallpaper` |
| W1 | `art` | - | As a user, I find six built-in wallpapers on my system disk, each a 4bpp tile and a 1bpp tile, made from art kept as source | `tools/os88wall.py` builds `build/wall/*.WAL` from `kernel/wallart/`; `t_wall` (fast) checks every file against SPEC 136.2; the files are in `SYSTEM\WALLPAPR\` on the system disks (360KB measured, SPEC 136.10) |
| W2 | `core` | - | As a user with a wallpaper chosen, I see it tiled behind every window on VGA, EGA, CGA, Hercules and VESA, on a second display too, and moving a window redraws only what it uncovered | `gfx_wall` + the `thm_desk` arm + `WALL.DRV` (three renderers) + boot and `vid_switch` apply + `'WP'` + `CPG_WALL` + `desk_bpat`; kern_big, kern_emu and kern_mega build; resident <= 128 bytes and no rung crossed on any kernel; one boot per adapter looked at; a gfxbench case exists |
| W3 | `cpw` | - | As a VGA/VESA user, I pick a wallpaper from a gallery of thumbnails on the wide Control Panel's Wallpaper page, or Browse... for a `.WAL` of my own | the page per the mockups, through `CPG_WALL`; looked at on VGA 640x480 and VESA 1024x768 |
| W4 | `cpc` | - | As an EGA, CGA or Hercules user, I pick a wallpaper on the compact Control Panel's Wallpaper page and see a preview at my screen's depth | the page per the mockups, through `CPG_WALL`; looked at on EGA and CGA; `CTRL.DRV` within its bound on every kernel |
| W5 | `pkg` | - | As a Desk Clock user, its transparent corners show the wallpaper, not a dither | `dk_gsample`'s rebuild replaced by `OSAPI_GFX_DESK` under a wallpaper (SPEC 136.5 item 4); looked at |
| W6 | lead | - | QA in emulation over every story and adapter, the fix round, the final pass, the measurements of SPEC 136.10 | QA list below empty or deferred with reasons; soak union + `test-full` green; PR body with screenshots per adapter |

## QA list

(The QA pass writes here: what was done, seen, expected, adapter, screenshot.)

## Deferred

(Nothing yet.)
