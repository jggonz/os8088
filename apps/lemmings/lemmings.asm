; =============================================================================
; os8088 - apps/lemmings/lemmings.asm
;
; The assembly shim of LEMMINGS (SPEC.md 116) - Lemmings (DMA Design /
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
; are in the converted band files (SPEC.md 116.2, 116.3) and in the .c files that
; read them, each naming its source at the line that carries it. The full
; attribution list is in README.TXT beside the package (SPEC.md 116.2).
; =============================================================================

; --- os88ui.inc's FEATURE DEFINES, AND THEY BELONG AT THE TOP -----------------
; Preprocessor tests are answered in FILE ORDER and os88ui.inc's own %include is
; at the END of this file, so a %define placed beside that include would sit
; BELOW every %ifdef that reads it: the guarded bodies assemble anyway - the
; binary GROWS - while the file's own guarded blocks silently vanish
; (SPEC.md 13.10.7.4, apps/weave/weave.asm's header says it at length).
%define OS88UI_ABFIT                ; ...whose pitch steps DOWN to fit the
                                    ; content box: the card is TWELVE rows
                                    ; (wave 5, SPEC.md 116.1), 158 pixels at
                                    ; the standard pitch of 12 against a
                                    ; CGA's 137, and 134 at 10. TameGram's
                                    ; opt-in (apps/os88ui.inc)
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
                                    ; before it starts (SPEC.md 116.6)

%define CC_PKG_NAME 'LEMMINGS'      ; <= 15 chars: the package's own name in the
                                    ; header. The overlay is a PART of this
                                    ; file (CC_OVL_PART below), so no module
                                    ; file name is built from it
%define CC_ICON "lemmings/icon.inc" ; the 16x16 walking lemming, drawn for this
                                    ; port (wave 6). 64 bytes of image, and the
                                    ; Disk window's tile instead of the generic
                                    ; ico_app16; crt0.asm checks its offset and
                                    ; length. A written prerequisite of
                                    ; build/lemmings.bin (LESSONS.md 9)

%define CC_HAS_ONKEY                ; void os88_onkey(int, int, void *)
%define CC_HAS_ONCLICK              ; void os88_onclick(int, int, void *)
%define CC_HAS_ONRESIZE             ; void os88_onresize(int, int, void *) -
                                    ; SPEC.md 11.98. The level list is PAGED
                                    ; from the LIVE content height and never
                                    ; from its own length (LESSONS.md 8's Font
                                    ; list), so a content box that changes under
                                    ; us changes the page
%define CC_HAS_ONWAKE               ; void os88_onwake(void *) - the one
                                    ; lock-free callback, and where the disk
                                    ; errands go: the progress file, a rating,
                                    ; the launch (SPEC.md 74.1,
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
%define CC_HAS_FSX                  ; the bracket's thunks (SPEC.md 53, 116.5):
                                    ; os88_fsx_caps() asked with OUR window is
                                    ; the predicate that greys the Mode row AND
                                    ; the bit os88_fsx_mode() would refuse on,
                                    ; SPEC.md 47 rule 4's one predicate for
                                    ; both; os88_fsx_run() is handed
                                    ; lem_fsx_main, a RESIDENT function. The
                                    ; C's own use set (SPEC.md 73.9.1) trims
                                    ; the thunks to the ones it names
%define CC_HAS_OVL                  ; ...and the overlay (SPEC.md 73.14,
                                    ; 116.6). The split is from wave 1 rather
                                    ; than from the wave the ceiling binds in:
                                    ; at cword's MEASURED 5.7 bytes a line this
                                    ; program does not fit one segment and
                                    ; never could
%define CC_OVL_PART                 ; ...which is PART 0 of LEMMINGS.O88 and
                                    ; not a LEMMINGS.OVL beside it (SPEC.md
                                    ; 73.14.2, CWORD's shape since main's
                                    ; drift), and EAGER: op_load reads it at
                                    ; launch, behind the image. A disk can no
                                    ; longer carry the package without its
                                    ; module - wave 1's "Play refuses in a
                                    ; window" case is gone with the file - and
                                    ; the folder is one row shorter. Every
                                    ; ovl_ call from inside the bracket finds
                                    ; it resident, which the bracket requires
                                    ; (SPEC.md 116.5)

%ifdef CC_OVL_LAZY                  ; THE PART HAS TO STAY EAGER, and this is
                                    ; the last line a define can precede crt0
                                    ; at, so nothing above slips past it. About
                                    ; eighty
%fatal "LEMMINGS: its void ovl_ functions require the EAGER part (SPEC.md 116.6)"
%endif                              ; ovl_ functions are `void` - ovl_tick_draw,
                                    ; ovl_frame_end, ovl_mm_clear among them,
                                    ; called inside the bracket - which breaks
                                    ; LESSONS.md's "an overlay function answers
                                    ; a status, 0 = it did not happen". That is
                                    ; safe ONLY because the eager part makes
                                    ; cc_ovneed a bare `ret`; under the lazy arm
                                    ; a refused fetch would make each of them
                                    ; silently do nothing, mid-frame

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

; --- THE RASTER (SPEC.md 116.4), and it is hand-written for two reasons -------
; Inside SPEC.md 53's bracket every kernel drawing slot is REFUSED, so the only
; way to put a pixel on the glass is a write into the framebuffer segment the
; FSI block names and a word with the card's own registers - and C can do
; neither: there is no C for `out dx, al`, and a per-pixel loop in C costs 3-5x
; what the same loop costs here (SPEC.md 73.11's rule that the inner loop is
; never C). The C above calls each of these once per piece, once per sprite or
; once per frame, and never per pixel.
;
; THE PATH IS RELATIVE TO nasm's -I apps/ (LESSONS.md 3): "lemmings/lemmask.inc"
; and never "lemmask.inc". Each is a written prerequisite of
; $(BUILD)/lemmings.bin in the Makefile, because make cannot see through a
; %include any more than it can see through a #include (LESSONS.md 9).
%include "lemmings/lemmask.inc"     ; the solid mask, and the probe
%include "lemmings/lemblit.inc"     ; the three backends
%include "lemmings/lemfont.inc"     ; text in a foreign mode (SPEC.md 85.7)
%include "lemmings/lemsprite.inc"   ; sprites, objects, the background, the
                                    ; pointer (SPEC.md 116.4.6)

    CC_IMAGE_END                    ; cc_bss_end, cc_modc_end and cc_image_end -
                                    ; the three forward references the header
                                    ; made

; ...and the icon, "lemmings/icon.inc", is %included by crt0.asm at the
; header's file offset 32 (CC_ICON above), not here.
