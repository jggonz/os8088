# How desktop wallpaper was built

A diary kept as the work happened (SPEC.md 136, `docs/plans/WALLPAPER-PLAN.md`).
Observations and intentions are kept apart; what was tested is named.

## 2026-11-23 - the brief and the one place a ground is laid

The owner asked for Windows 3.1-style wallpaper with one condition: redraw
only what is uncovered, a tiled 4bpp picture plus a 1bpp version, or it is
the full-screen repaint PERFORMANCE.md Part 5 counts as a regression.

Two read-only scouts mapped the ground before anything was designed.
**Observed:** every ground site already goes through `thm_desk` (SPEC 76.2)
with a rect `wm_dmg_gray` has cut to the uncovered region, so the condition
is met by construction IF the wallpaper is laid there and nowhere else, and
if its phase is a function of the absolute position (the dither's own
property). Four sites assumed the ground was a dither or a colour
(`desk_bpat`, its zone gate, the Theme cards, Desk Clock's corner sampling);
each became an item of a story. A held module costs about 27 resident bytes
for its door (WIN95.DRV's shape); kern_small has no theme and reads no
SYSTEM.CFG, so it has no wallpaper.

Codex CLI 0.170.0 drew the four Control Panel mockups (`docs/wallpaper/`)
and the six tile designs (`kernel/wallart/`) from prompts kept beside them.
