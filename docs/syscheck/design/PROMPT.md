# System Check design mockups - the prompt

These three mockups are the DESIGN for System Check (SPEC.md 118), made
before any code, as CLAUDE.md's "Treat artwork as source" asks. They are a
guide to layout and look, not a runtime asset: nothing in `make` reads them.

- Tool: `codex exec` (codex-cli 0.163.0, built-in `image_gen`), run from a
  scratch directory with the prompt below on stdin and four real os8088
  screenshots as `-i` references: `redline-summary.png`,
  `redline-running.png`, `hddtool-drv-install.png` and `taskmgr-window.png`
  from the website's guide (`public/guide/shots/`).
- `CODEX-PROMPTS.md` is what Codex itself then handed its image tool, one
  prompt per picture, kept verbatim.
- Where the shipped window differs from a mockup, SPEC.md 118 says so and
  why (the "This System" card cannot name the RUNNING kernel's build, for
  one: no slot reports it, SPEC.md 57.3).

The prompt as given:

---

Use your image generation tool to create THREE UI design mockups for a new
application called "System Check" that runs on os8088 - a Macintosh System
1-style GUI operating system for the IBM PC/XT (8088 CPU), written in 8086
assembly. The attached screenshots are REAL os8088 screens (REDLINE, a
benchmark app; the hard-disk Install dialog; the Task Manager). Match their
visual language exactly: a 640x480 VGA screen, 8x8 pixel bitmap font only
(chunky, monospaced, all text on an 8-pixel grid), 1-pixel black rules,
striped Mac-style title bars with a close box, white ground, a menu bar at
the top (apple-style logo glyph at left, the app name, a clock at right), a
dock strip at the bottom. Hard pixel edges, no anti-aliasing, no gradients,
no shadows, no rounded corners, no photographic or modern flat-UI styling.
Only the 16 standard EGA/VGA colours, used sparingly as in REDLINE (black,
white, blue, green, red, brown/amber, dark grey, light grey).

WHAT THE APP DOES: it scans the BOOT volume of a hard-disk installation of
os8088 (drive C:) after the OS has been upgraded in place, and finds broken
or stale parts of the operating system: packages (.O88 programs) built for a
different package format than the running kernel (they will refuse to
load), drivers (.DRV) built for a different driver version, files left over
from previous upgrades (e.g. KPAD.TMP from an interrupted install, a
hibernation file from another build, programs this release retired such as
MODPLUG.O88), files this release ships that are missing, a boot sector file
(BOOTHD.SYS) that does not match the running kernel, and ASSOC.DAT file
associations pointing at programs that no longer exist. It shows progress
the way REDLINE does, with a progress bar and a results list.

Window layout (one window filling the screen under the menu bar, titled
"SYSTEM CHECK", like REDLINE's summary screen):
- Top-left card "Checking C: - boot volume, hard disk": a wide progress bar
  (REDLINE style: framed white bar, solid colour fill, tick labels below
  replaced by "0%  25%  50%  75%  100%"), the current path being read e.g.
  "C:\APPS\GAMES\TANK.O88", and counters "Files 214 of 412   Folders 37".
- Top-right card "This System": label/value pairs in two columns -
  "Kernel build  2190", "Package fmt   10", "Driver ver    11",
  "Boot volume   C: FAT16", "Checker build 2190", "Free KB  26,112".
- Middle, the largest card "Checks": one row per check, each with a small
  square status lamp at left (green = OK, amber = warning, red = broken,
  hollow grey = not yet run, a small animated marker on the one running),
  the check name, and a right-aligned result. Rows:
    Kernel and boot sector ........ OK
    Drivers (14) .................. 1 broken
    Packages (61) ................. 3 will not load
    Install leftovers ............. 2 found
    Retired programs .............. 1 found
    Missing release files ......... OK
    File associations ............. 4 stale
    Hibernation image ............. OK
- Bottom card "Findings - Packages" listing the selected check's findings,
  one per line, path then reason, with a scroll bar like REDLINE's:
    C:\APPS\OLDGAME.O88   format 8, kernel takes 10 - will not load
    C:\APPS\SKETCH.O88    format 9, kernel takes 10 - will not load
    C:\MEDIA\TOOL.O88     damaged header - not a package
- A bottom row of framed push buttons like REDLINE's: "Scan", "Details",
  "Save Report...", "Clean Up...", "Quit", and at the far right a status
  text such as "3 broken, 7 warnings".

The three images:
1. system-check-scanning.png - the scan IN PROGRESS at about 52%: the
   first three checks have lamps, the Packages row is running, the rest are
   hollow/pending; the Findings card shows what has been found so far.
2. system-check-results.png - the scan COMPLETE, every lamp lit as listed
   above, the Packages row highlighted (inverse video black bar with white
   text, as in the Install dialog's selected slot) and its findings shown.
   The progress card now reads "Done - 412 files in 37 folders, 1 min 48 s".
3. system-check-mono.png - the SAME completed screen as image 2, but on a
   monochrome Hercules adapter: 720x348 pixels, pure black and white only
   (1 bit per pixel). Colour lamps become distinct black/white glyphs
   (filled square = broken, checkerboard-dithered square = warning, small
   tick = OK, hollow square = pending) and the progress fill is a 50%
   dither or solid black. Everything still on the 8x8 font grid; the
   layout is compressed vertically to fit 348 rows.

All text must be spelled exactly as given, legible, in the blocky 8x8 font.
No logos, no watermarks, no extra decoration. Save each PNG into the
current directory under the file name given above and print the paths.
