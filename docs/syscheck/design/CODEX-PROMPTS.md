# System Check image generation prompts

Tool: built-in image_gen.imagegen.

Reference screenshots: redline-summary.png, redline-running.png, hddtool-drv-install.png, taskmgr-window.png.

## Initial completed view

Use case: ui-mockup.
Create ONE finished screen image for "System Check", an application for os8088. This image is the COMPLETE scan state, intended filename system-check-results.png. Canvas exactly 640 x 480 pixels, a direct unscaled framebuffer screenshot with the screen filling the image edge to edge.

Style reference: the four attached real os8088 screenshots (REDLINE summary, REDLINE running, Install selected slot, Task Manager). Match their exact 1984 Macintosh System 1 GUI on IBM PC/XT look: white ground, thin 1-pixel black rectangular rules, alternating black/white horizontal stripes in title bar, tiny square close box at upper left, square zoom box right, chunky 8x8 pixel MONOSPACED ROM bitmap text only. All text baselines on an 8-pixel grid. EVERY letter is tiny, crisp and legible. No anti-aliasing, smooth outlines, modern UI, gradients, shadows, bevels, rounded corners, photographic effects, or invented decoration. Use ONLY the standard 16 EGA/VGA colours: mostly pure white and black with small pure green, red, blue, brown/amber and grey areas. The screenshot should look like the references, not a modern recreation.

Overall geometry:
At y=0..17 a thin white menu bar with a tiny apple-style black glyph at x=8, menu text "System Check" at x=40, clock "00:00" at the far right.
One main window x=7..632, y=20..455. Its black/white striped title bar at y=23..36 has centered text "SYSTEM CHECK" on white, a close box at left and a tiny zoom box at right.
At y=456..479 a plain dock strip separated by a black rule and two small framed classic pixel tool icons at lower left, like REDLINE running. No other logos or extra decoration.

Top row: two framed white cards, with thin black outlines and small bitmap text.
LEFT card x=16..375, y=48..167:
Heading at x=24,y=56: "Checking C: - boot volume, hard disk"
Framed progress bar x=24..367,y=80..94, 100% filled solid dark EGA blue (#0000AA). Tick labels below at y=104: "0%" "25%" "50%" "75%" "100%" evenly spaced.
Status at x=24,y=128: "Done - 412 files in 37 folders,"
Next line x=24,y=144: "1 min 48 s"
The two lines together must say exactly "Done - 412 files in 37 folders, 1 min 48 s".

RIGHT card x=384..623,y=48..167:
Header x=392,y=56: "This System"
Six 8x8 bitmap rows at y=72,88,104,120,136,152 with two aligned columns. Labels and values verbatim:
"Kernel build"    "2190"
"Package fmt"     "10"
"Driver ver"      "11"
"Boot volume"     "C: FAT16"
"Checker build"   "2190"
"Free KB"         "26,112"

MIDDLE largest card x=16..623,y=176..343:
Heading at x=24,y=184 "Checks".
Eight rows at y=208,224,240,256,272,288,304,320. Each row contains an 8x8 square status lamp at x=24; then check name at x=48, dotted leader across middle, and a RIGHT ALIGNED result ending at x=608. Exact text rows:
"Kernel and boot sector" ........ "OK"             [green lamp]
"Drivers (14)" .................. "1 broken"       [red lamp]
"Packages (61)" ................. "3 will not load" [red lamp]
"Install leftovers" ............. "2 found"        [brown/amber lamp]
"Retired programs" .............. "1 found"        [brown/amber lamp]
"Missing release files" ......... "OK"             [green lamp]
"File associations" ............. "4 stale"        [brown/amber lamp]
"Hibernation image" ............. "OK"             [green lamp]
The Packages row at y=240 is selected: a full-width solid BLACK rectangular selection bar x=20..619 with WHITE 8x8 bitmap text. This inverse-video bar must resemble the black selected Slot 1 row in the Install dialog reference. Its small red lamp stays visible. Every lamp is lit, none pending.

BOTTOM findings card x=16..623,y=352..415:
Heading x=24,y=360 "Findings - Packages"
Three exact finding lines, one per line, in 8x8 bitmap font at y=376,392,408:
"C:\APPS\OLDGAME.O88   format 8, kernel takes 10 - will not load"
"C:\APPS\SKETCH.O88    format 9, kernel takes 10 - will not load"
"C:\MEDIA\TOOL.O88     damaged header - not a package"
The lines must fit the width with no clipping. Keep Windows paths and backslashes exact. Place a thin classic vertical scrollbar at x=608..620 inside this card, with pixel up/down arrows and striped track like REDLINE's results scrollbar.

Bottom controls at y=424..443, one row of white, black-framed square pushbuttons, text centered in the same 8x8 font. Labels exactly:
"Scan", "Details", "Save Report...", "Clean Up...", "Quit"
Keep enough room for a right-aligned status at far right: "3 broken, 7 warnings"
Suggested widths: Scan 48, Details 72, Save Report... 128, Clean Up... 104, Quit 48, with 4-pixel gaps, starting x=16; status at x=464.
Use ASCII three dots in button labels, not a Unicode ellipsis.

Typography takes precedence: spell every quoted text string exactly. The whole screenshot is a dense small utility, with the same text scale and 1-pixel construction as the attached screenshots. Do not add big titles, subtitles, icons in cards, explanatory captions, legends, watermark, or perspective. One complete scan screenshot only.

## Completed view refinement

Use case: ui-mockup / precise pixel-style refinement.
Image 1 is the System Check completed-screen EDIT TARGET. Images 2 and 3 are REAL os8088 screenshots, STYLE REFERENCES ONLY.
Keep the entire UI content and geometry of Image 1 unchanged, including every word, row, result, packages inverse selection, all three findings, both cards, all five buttons, and bottom status. Refine its typography and rendering to faithfully match Images 2 and 3.
CRITICAL: replace the larger tall sans-serif bitmap-like lettering of Image 1 with the actual TINY FAT 8x8 MONOSPACED BITMAP lettering seen in Image 2 REDLINE and Image 3 Task Manager. The reference glyphs are squat, bold, often slab-serif, with fat vertical stems and short horizontal strokes. Every character occupies EXACTLY one 8x8 pixel cell of a 640x480 framebuffer; black/white pixels, no smoothing. Copy the font appearance of the references closely. Text top-left origins are on an 8-pixel lattice. All frames are thin 1-logical-pixel black outlines. Title bar is a set of one-pixel horizontal stripes with a small EMPTY square close box, not an X, just like Image 2. Keep the apple-style menu glyph small.
Requested output exactly 640x480 screen pixels, NO resampling smoothing, NO anti-aliasing, NO gradients. Black is #000000, white #FFFFFF, progress blue #0000AA, green lamps #00AA00, red #AA0000, amber #AA5500. Use standard EGA palette only. All broad white areas must be perfectly constant white, not paper texture or grey shading. It must look like a native low-resolution 1980s PC screenshot, not enlarged modern terminal text.

Text must stay exact:
Checking C: - boot volume, hard disk
This System
Kernel build 2190
Package fmt 10
Driver ver 11
Boot volume C: FAT16
Checker build 2190
Free KB 26,112
Done - 412 files in 37 folders, 1 min 48 s
Checks
Kernel and boot sector ... OK
Drivers (14) ... 1 broken
Packages (61) ... 3 will not load
Install leftovers ... 2 found
Retired programs ... 1 found
Missing release files ... OK
File associations ... 4 stale
Hibernation image ... OK
Findings - Packages
C:\APPS\OLDGAME.O88   format 8, kernel takes 10 - will not load
C:\APPS\SKETCH.O88    format 9, kernel takes 10 - will not load
C:\MEDIA\TOOL.O88     damaged header - not a package
Scan
Details
Save Report...
Clean Up...
Quit
3 broken, 7 warnings
Font must be an 8x8 bitmap only throughout, not Courier or a modern typewriter font. No new text or features.

## Scanning view

Use case: ui-mockup / precise-object-edit.
Image 1 is the EDIT TARGET: a completed System Check screen, native os8088 Macintosh System 1 style on IBM PC/XT. Make a MATCHING SCAN IN PROGRESS screenshot. Keep the same precise layout, typography, all panel borders, menus, titlebar, tiny dock, buttons, palette and every unchanged word. Intended filename system-check-scanning.png. Exactly 640x480 native pixels, hard edges, ZERO anti-aliasing, only standard EGA colours, all glyphs squat heavy 8x8 monospaced bitmap like the reference, 1px rules, white background.

Change ONLY these state details:
1. Progress blue fill is now 52% of the framed bar. Remaining 48% perfectly white. Keep tick labels "0%" "25%" "50%" "75%" "100%".
2. Replace the two-line Done message in the upper left card with EXACTLY these two lines:
"C:\APPS\GAMES\TANK.O88"
"Files 214 of 412   Folders 37"
Keep card heading exactly "Checking C: - boot volume, hard disk".
3. In Checks, first row "Kernel and boot sector" has a green square lamp and result "OK"; second row "Drivers (14)" has a red square lamp and result "1 broken"; third row "Packages (61)" has a red square lamp and result "3 will not load". The Packages row is RUNNING: remove the completed black selected-row bar so it is white with black text, and add a tiny black spinner-frame glyph immediately alongside its red status lamp (a single phase of a small animated marker).
4. The remaining FIVE checks are still waiting. Each must have a hollow grey square lamp with white center, and "--" at the far right instead of the completed result. Keep their names EXACTLY:
"Install leftovers"
"Retired programs"
"Missing release files"
"File associations"
"Hibernation image"
No amber or green or red lamps on those five pending rows.
5. Keep the findings already discovered in the bottom "Findings - Packages" card, all three exact lines, with scrollbar:
"C:\APPS\OLDGAME.O88   format 8, kernel takes 10 - will not load"
"C:\APPS\SKETCH.O88    format 9, kernel takes 10 - will not load"
"C:\MEDIA\TOOL.O88     damaged header - not a package"
6. Change far-right footer status to "3 broken, 0 warnings".

This System remains exactly:
Kernel build 2190
Package fmt 10
Driver ver 11
Boot volume C: FAT16
Checker build 2190
Free KB 26,112

Buttons still "Scan" "Details" "Save Report..." "Clean Up..." "Quit", exact ASCII periods. Window title "SYSTEM CHECK", menu app name "System Check", clock "00:00". Small empty square close box, thin striped titlebar. No new UI, labels or art. No watermark. Preserve the tiny squat bitmap font and exact compact utility style of Image 1.

## Monochrome Hercules view

Use case: ui-mockup / precise-object-edit.
Image 1 is the EDIT TARGET: the completed System Check screen. Create the SAME COMPLETED SCREEN for monochrome Hercules hardware. Intended filename system-check-mono.png.
CRITICAL output dimensions exactly 720 x 348 native pixels (Hercules framebuffer 90 columns of 8px text), very wide aspect ratio 720:348. PURE BLACK #000000 AND WHITE #FFFFFF ONLY, 1 bit per pixel. ZERO grey, ZERO colour, ZERO anti-aliasing. Render as a direct native low-resolution screenshot, no monitor or frame around the framebuffer. Hard 1-pixel lines, squat heavy 8x8 monospaced bitmap text with all text placed on an 8-pixel grid. Match the same os8088 System 1 style of the source.

Reflow the source vertically to fit 348 rows, using this compact layout, while keeping ALL content:
Menu bar y=0..15, tiny apple-style system glyph left, app name "System Check", clock "00:00" far right.
Main window x=7..712, y=16..327, striped titlebar y=16..31, title "SYSTEM CHECK", tiny empty close box left, zoom box right.
Bottom dock strip y=328..347 with two small square classic pixel tool icons at bottom left.

Top left card x=16..455,y=40..127. Title x=24,y=48 exactly "Checking C: - boot volume, hard disk".
Progress bar x=24..447,y=64..79, COMPLETE, fill with true alternating black/white 1-pixel 50% CHECKERBOARD DITHER. Thin black frame.
Tick labels "0%" "25%" "50%" "75%" "100%" at y=80, evenly spaced.
Done status x=24,y=104 exactly "Done - 412 files in 37 folders, 1 min 48 s" on ONE 8x8 text line. It fits 53 text cells.

Top right card x=464..703,y=40..127:
header x=472,y=48 "This System"
Six aligned label/value rows at y=64,72,80,88,96,104:
"Kernel build" "2190"
"Package fmt" "10"
"Driver ver" "11"
"Boot volume" "C: FAT16"
"Checker build" "2190"
"Free KB" "26,112"

Largest middle Checks card x=16..703,y=136..231. Heading x=24,y=144 "Checks".
EIGHT dense rows, y=160,168,176,184,192,200,208,216, font 8x8 with no extra leading.
Names at x=48, distinct 8x8 B/W status glyphs at x=24, dotted leaders to right aligned results at x=688.
Rows, exact words, and status glyph:
"Kernel and boot sector" .... "OK" [small black bitmap CHECKMARK]
"Drivers (14)" .............. "1 broken" [solid black FILLED SQUARE]
"Packages (61)" ............. "3 will not load" [solid FILLED SQUARE; inverse white on black selection]
"Install leftovers" ......... "2 found" [black-white CHECKERBOARD DITHERED SQUARE]
"Retired programs" .......... "1 found" [CHECKERBOARD DITHERED SQUARE]
"Missing release files" ..... "OK" [small black CHECKMARK]
"File associations" ......... "4 stale" [CHECKERBOARD DITHERED SQUARE]
"Hibernation image" ......... "OK" [small black CHECKMARK]
The Packages row is selected, SOLID BLACK full-width rectangular selection with PURE WHITE bitmap lettering, like source. Invert the filled-square broken glyph to white within that selected row to keep its filled-square meaning visible. No pending hollow squares because the scan is complete. The checkmarks, filled squares and checkerboard squares must be visually distinct and legible at native resolution.

Findings card x=16..703,y=240..295:
Heading x=24,y=248 "Findings - Packages".
Three exact single-line findings at y=264,272,280:
"C:\APPS\OLDGAME.O88   format 8, kernel takes 10 - will not load"
"C:\APPS\SKETCH.O88    format 9, kernel takes 10 - will not load"
"C:\MEDIA\TOOL.O88     damaged header - not a package"
Place classic thin vertical scrollbar at right inside card, tiny black up/down triangles and alternating pixel track.

Bottom pushbuttons y=304..319, labels in 8x8 cells, thin square black frames, NO rounded corners:
"Scan" "Details" "Save Report..." "Clean Up..." "Quit".
At far right status exactly "3 broken, 7 warnings".
Same all-complete data as Image 1. All text must be VERBATIM, no clipping. This is compressed Hercules screen, NOT the original 640x480 layout simply stretched or cropped. Do not lose any of the eight Checks rows or any finding. No added decoration, legend, caption, brand logo, watermark, large font, grey dithering simulation, or modern styling. The 50% pattern must consist of individual PURE black and white pixels.

