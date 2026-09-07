; =============================================================================
; os8088 - apps/lemmings/lemmings.asm
;
; The assembly shim of LEMMINGS (SPEC.md 92) - Lemmings (DMA Design /
; Psygnosis, 1991, the DOS release) reimplemented as an os8088 C package. It is
; the top-level nasm source: apps/lemmings/lemmings.c is never assembled on its
; own, because `nasm -f bin` has no notion of an external symbol, so the
; compiled C, the runtime and the 32-byte header are ONE assembly (SPEC.md
; 73.1).
;
; A shim does four things and nothing else belongs in it: name the package,
; declare which callbacks the C actually defines, include the runtime and then
; the compiled C in that order (the header has to be at file offset 0), and
; close the image. The two halves cannot drift silently - a %define with no C
; function behind it is an nasm external-reference error naming that function,
; and a C function with no %define is code the kernel never calls.
;
; ATTRIBUTION. Lemmings is (C) 1991 DMA Design / Psygnosis. Nothing in this
; file is derived from the original; the derived tables, strings and offsets
; are in the converted band files (SPEC.md 92.2, 92.3) and in the .c files that
; read them, each naming its source at the line that carries it. The full
; attribution list is in README.TXT beside the package (SPEC.md 92.2).
; =============================================================================

; --- os88ui.inc's FEATURE DEFINES, AND THEY BELONG AT THE TOP -----------------
; Preprocessor tests are answered in FILE ORDER and os88ui.inc's own %include is
; at the END of this file, so a %define placed beside that include would sit
; BELOW every %ifdef that reads it: the guarded bodies assemble anyway - the
; binary GROWS - while the file's own guarded blocks silently vanish
; (SPEC.md 13.10.7.4, apps/weave/weave.asm's header says it at length).
%define OS88UI_ABOUT                ; the standard About card (SPEC.md
                                    ; 20.5.1.1), reached from C through
                                    ; os88_about_card()
%define OS88UI_NOBTN                ; ...and NOTHING else, which is
                                    ; apps/tank/tank.asm's pair. This program's
                                    ; only clickable chrome is the Play control
                                    ; and its two-line hit test is in lemui.c,
                                    ; so os88ui's button drawer and its arm word
                                    ; would be bytes with no caller - and this
                                    ; package's resident budget is 81% spent
                                    ; before it starts (SPEC.md 92.6)

%define CC_PKG_NAME 'LEMMINGS'      ; <= 15 chars, and the stem of LEMMINGS.OVL:
                                    ; crt0.asm builds the module's file name out
                                    ; of this and '.OVL', so the disk and the
                                    ; loader cannot disagree about it

%define CC_HAS_ONKEY                ; void os88_onkey(int, int, void *)
%define CC_HAS_ONCLICK              ; void os88_onclick(int, int, void *)
%define CC_HAS_ONRESIZE             ; void os88_onresize(int, int, void *) -
                                    ; SPEC.md 11.98. The level list is PAGED
                                    ; from the LIVE content height and never
                                    ; from its own length (LESSONS.md 8's Font
                                    ; list), so a content box that changes under
                                    ; us changes the page
%define CC_HAS_ONWAKE               ; void os88_onwake(void *) - the FIRST
                                    ; callback, and the only place the overlay
                                    ; may be forced resident: there is no
                                    ; instance to resolve a module for while
                                    ; os88_main() is running (SPEC.md 73.14,
                                    ; LESSONS.md 13)
%define CC_HAS_ONTIMER              ; void os88_ontimer(void *) - SPEC.md 13.9,
                                    ; ONE-SHOT, and it exists for exactly one
                                    ; thing: the preview pane SETTLES. An arrow
                                    ; key changes two level rows AND the seven
                                    ; preview lines beside them, and the pane is
                                    ; ~140 of the ~180 glyph cells a selection
                                    ; move costs - ~126 ms of the ~168 on a
                                    ; 4.77 MHz 8088. Held down, the machine
                                    ; falls that far behind on every repeat,
                                    ; which is INPUT OVERRUN (PERFORMANCE.md
                                    ; Part 1's third emulator-invisible defect).
                                    ; So the list rows go down on the keystroke
                                    ; and the pane is armed for three ticks and
                                    ; re-armed by each further key. The slot is
                                    ; REFUSABLE on a kern_small machine
                                    ; (SPEC.md 13.8.2), so lemui.c's
                                    ; lem_pv_defer() reads os88_wm_timer()'s
                                    ; answer and draws the pane now if it is
                                    ; refused
%define CC_HAS_MENUS                ; void os88_oncmd(int, int, void *) - and it
                                    ; can never be called. The registered set is
                                    ; EMPTY and exists so its AM_NAME puts
                                    ; 'Lemmings' in the kernel bar instead of
                                    ; this header's 'LEMMINGS' (SPEC.md 12.2,
                                    ; 73.12; apps/word's idiom). A set with no
                                    ; menus has no item to pick, so the handler
                                    ; is a stub - but the %define has to be here
                                    ; anyway, because os88_menu_set() is the
                                    ; slot that patches the trampoline in
%define CC_HAS_ABOUT                ; void os88_about(void *)
%define CC_HAS_FSX                  ; void os88_fsx_main(void *) and the seven
                                    ; thunks (SPEC.md 92.5). WAVE 1 USES EXACTLY
                                    ; ONE OF THEM: os88_fsx_caps(), asked with
                                    ; OUR window, is the predicate that greys the
                                    ; Mode row AND the bit os88_fsx_mode() would
                                    ; refuse on, which is SPEC.md 47 rule 4's
                                    ; one predicate for both. The bracket itself
                                    ; arrives with the raster in wave 2, so
                                    ; os88_fsx_main() is declared, defined and
                                    ; not yet called - exactly as os88_oncmd()
                                    ; above is
%define CC_HAS_OVL                  ; ...and LEMMINGS.OVL (SPEC.md 73.14, 92.6).
                                    ; The split is from wave 1 rather than from
                                    ; the wave the ceiling binds in: at cword's
                                    ; MEASURED 5.7 bytes a line this program does
                                    ; not fit one segment and never could

%include "cc/crt0.asm"              ; the sections, the 32-byte header, the entry
                                    ; and callback trampolines, the overlay
                                    ; runtime and the whole API bridge (it
                                    ; %includes apps/cc/os88thunk.asm)

%include "lemmings.gen.asm"         ; the compiled C, found through -I build/

; --- the shared control library ----------------------------------------------
; AFTER crt0.asm and after the compiled C. The "at the END of your code" rule in
; os88ui.inc's own header protects the ICON's fixed offset (SPEC.md 20.2) in an
; ASSEMBLY package; crt0.asm emits the header, the icon and the association
; block itself and carries all four offset assertions inside it, so that hazard
; cannot reach a C package at all (apps/weave/weave.asm:126-144 works this
; through). What the position here does buy is the ordinary one: nothing above
; names an OS88UI_* constant in a %if or a resb, so there is nothing to
; forward-resolve.
%include "os88ui.inc"

    CC_IMAGE_END                    ; cc_bss_end, cc_modc_end and cc_image_end -
                                    ; the three forward references the header
                                    ; made

; WAVE 2 ADDS THREE %includes HERE, and each is a written prerequisite of
; $(BUILD)/lemmings.bin in the Makefile before it exists, because make cannot
; see through a %include either (LESSONS.md 9):
;   %include "lemmings/lemblit.inc" ; the three rasters (SPEC.md 92.4)
;   %include "lemmings/lemmask.inc" ; the solid-mask primitives
;   %include "lemmings/lemfont.inc" ; text in a foreign mode (SPEC.md 85.7)
; ...and wave 6 adds "lemmings/icon.inc" beside them.
