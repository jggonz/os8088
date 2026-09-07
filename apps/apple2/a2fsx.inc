; =============================================================================
; os8088 - apps/apple2/a2fsx.inc    the foreign-mode raster writers - A STUB
;
; Part of APPLE2, an Apple II Plus emulator (docs/APPLE2-SPEC.md).
; apps/apple2/ is GPL-2-or-later (apps/apple2/COPYING).
;
; ----------------------------------------------------------------------------
; THIS FILE IS EMPTY ON PURPOSE, AND IT EXISTS ON PURPOSE
; ----------------------------------------------------------------------------
; The foreign video mode is WAVE 5 (APPLE2-SPEC section 13): SPEC.md 53's
; exclusive bracket, reached through the four `OSAPI_FSX_*` slots, with
; FSXM_VGA13 carrying the port's colour and FSXM_CGA640 / FSXM_HERC shipping
; ONLY IF THEY MEASURE FASTER THAN THE WINDOWED PATH on the XT tier - they are
; 560 x 192 = 13,440 bytes a frame against the windowed 7,680, so "an XT gets
; its speed back" is a claim to prove and not to assert.
;
; TWO THINGS THAT BIND WHOEVER WRITES IT, stated here because this is the file
; they will open:
;
;  - EVERY FOREIGN FRAME IS DRIVEN OFF THE SAME DIRTY-LINE SET THE WINDOWED
;    FLUSH COMPUTES, against a foreign-frame shadow in a heap claim taken at
;    the fullscreen LATCH (section 13.2). A full raster write per frame is
;    precisely what the dirty-page bitmap, the write window, the scan-line map
;    and the span compare exist to avoid, and VGA13 at 280 x 192 with a
;    per-pixel artifact index is ~300 ms a frame on a 4.77 MHz 8088.
;  - THE BRACKET'S RULES ARE NOT CHECKABLE FROM C AND ARE THE WHOLE FEATURE
;    (section 13.3), which is why the bracket lives in ONE function whose
;    header is the rule list.
;
; The file is here from wave 1 because it is a WRITTEN PREREQUISITE in the
; Makefile and a `%include` in apps/apple2/apple2.asm: make cannot see through
; either kind of include, and a file a later wave adds is a build the tree
; does not know about (LESSONS.md 9).
; =============================================================================
