; =============================================================================
; os8088 - apps/weave/weave.asm
;
; The assembly shim of WEAVE, the .WAB runtime (WEAVE-SPEC 1.2). It is
; the top-level nasm source: the .c files are never assembled on their own,
; because `nasm -f bin` has no notion of an external symbol, so the compiled C,
; the runtime, the hand-written cores and the 32-byte header are ONE assembly
; (SPEC.md 73.1).
;
; A shim does four things and nothing else belongs in it: name the package,
; declare which callbacks the C actually defines, include the runtime and then
; the compiled C in that order (the header has to be at file offset 0), and
; close the image. The two halves cannot drift silently - a %define with no C
; function behind it is an nasm external-reference error naming that function,
; and a C function with no %define is code the kernel never calls.
;
; IT IS NOT LOOM. LOOM.O88 is a separate package (WEAVE-SPEC 1.2) - the
; WORD/CWORD precedent, that two things may not answer to one name - and
; nothing here may reach a `loom` name.
; =============================================================================

; --- os88ui.inc's FEATURE DEFINES, AND THEY BELONG AT THE TOP ----------------
; Preprocessor tests are answered in FILE ORDER, and os88ui.inc's own include
; is at the very END of this file by its contract (see there). A %define placed
; beside that include is BELOW every %ifdef that reads it: the guarded bodies
; assemble anyway - the binary GROWS - while each of the file's own guarded
; blocks silently vanishes, which is 405 bytes and a thumb that does not move
; (SPEC.md 13.10.7.4). So they are here, at the top, and the include is at the
; bottom, and the distance between the two is the whole point.
;
; WEAVE-SPEC 6.1's component library needs two of the three features:
%define OS88UI_SCROLL               ; os88ui_sbar/sbhit/sbmove and the
                                    ; OS88UI_SB* answers - <list> and <grid>
%define OS88UI_ARM                  ; os88ui_arm/fire/armed - the press/release
                                    ; gesture every <button>, <check> and
                                    ; <radio> is armed through
                                    ;
                                    ; NOT %define OS88UI_SBDRAG: nothing drags
                                    ; a thumb yet, and the state it declares is
                                    ; six bytes of bss and a body. Add it here,
                                    ; not beside the include.

%define CC_PKG_NAME 'WEAVE'         ; <= 15 chars, and the stem of WEAVE.OVL:
                                    ; crt0.asm builds the module's file name
                                    ; out of this and '.OVL', so the disk and
                                    ; the loader cannot disagree about it

%define CC_HAS_ONKEY                ; void os88_onkey(int, int, void *)
%define CC_HAS_ONCLICK              ; void os88_onclick(int, int, void *)
%define CC_HAS_ONRESIZE             ; void os88_onresize(int, int, void *) -
                                    ; SPEC.md 11.98, and NOT the same thing as
                                    ; WM_ONSIZE: that one is asked BEFORE a
                                    ; resize and takes an answer, this one is
                                    ; told after and must not draw
%define CC_HAS_MENUS                ; void os88_oncmd(int, int, void *)
%define CC_HAS_ABOUT                ; void os88_about(void *)
%define CC_HAS_FDLG                 ; void os88_onfile(...) - File > Open, the
                                    ; second of WEAVE-SPEC 1.5's ways in
%define CC_HAS_OVL                  ; ...and WEAVE.OVL (SPEC.md 73.14), whose
                                    ; tenants are in apps/weave/wovl.c
                                    ;
                                    ; and NOT, in this wave: CC_HAS_ONMOUSEUP
                                    ; (nothing is armed yet), CC_HAS_ONWAKE
                                    ; (the WVM's slices arrive with the VM) or
                                    ; CC_HAS_WORKER (the canvas worker arrives
                                    ; with <canvas>) - a trampoline nobody asks
                                    ; for is not assembled at all

%define CC_ICON  "weave/icon.inc"   ; 32 dw rows exactly: 16 mask, 16 data
%define CC_ASSOC "weave/wvassoc.inc" ; 'WAB' - so a bundle opens on the FIRST
                                    ; double-click, with no prior run and no
                                    ; search (SPEC.md 54.6, WEAVE-SPEC 1.5)

%include "cc/crt0.asm"              ; the sections, the 32-byte header, the
                                    ; icon and association blocks, the entry
                                    ; and callback trampolines, the overlay
                                    ; runtime and the whole API bridge

%include "weave.gen.asm"            ; the compiled C, found through -I build/

; --- THE SHARED CONTROL LIBRARY, BEFORE THE CORES THAT NAME ITS CONSTANTS ----
; os88ui.inc is the standard control (SPEC.md 20.5.1) and os88line.inc the
; one-line field behind every <input> (WEAVE-SPEC 6.7). Both are included HERE
; - after crt0.asm, before wdraw.inc - and the position is load-bearing in two
; directions at once.
;
; WHY THIS IS NOT THE VIOLATION IT LOOKS LIKE. os88ui.inc's header says "at the
; END of your code, not up beside os88api.inc", and the reason it gives is the
; icon: the 32-byte header and the OS88_ICON16 block are at FIXED OFFSETS
; (SPEC.md 20.2), so an assembly package that includes it early emits code
; between them and fails the icon macro's offset assertion. THAT HAZARD CANNOT
; REACH A C PACKAGE AT ALL. crt0.asm emits the header, the icon and the
; association block itself and carries all four assertions inside it
; (apps/cc/crt0.asm's `%if ($ - $$) != 32` / `!= 96` pairs), and they are
; decided during the %include above - so nothing placed after that line can
; violate them. What the "at the end" convention actually protects is
; narrower: your own %ifdef gates and any %assign naming an os88ui constant
; must sit below the include (apps/notepad/notepad.asm says this about
; OS88UI_AMAX). Neither binds our position relative to our own .inc files.
;
; NOT LATER, and this is the half that is easy to get wrong. wdraw.inc names
; two things that are not labels: OS88UI_SB* in a %if (its drift-guard against
; os88ui.inc renumbering the scroll bar's parts) and OS88LINE_SZ in a `resb`.
; nasm forward-resolves a LABEL - every `call os88ui_btn` in that file would
; have been fine either way - but it evaluates a %if where it stands and sizes
; a `resb` where it sees it. With -w+error=forward the second one is an error
; rather than a scratch block of the wrong size under every input the painter
; draws, which is the failure that would have shipped.
;
; os88line.inc after os88ui.inc: it calls into that file, and its header says
; so. apps/browser/browser.asm is the worked example of this exact pair.
%include "os88ui.inc"
%include "os88line.inc"

; --- the hand-written cores (SPEC.md 73.11: the inner loop is assembly) ------
; AFTER the compiled C, because the C names them and nasm resolves a forward
; reference to a label but not a section that has not been opened yet. Their
; own headers carry the segment rules; both leave DS, ES and DF as they found
; them, and both switch back to .text before they end (CLAUDE.md's section
; discipline - the next %include otherwise lands in the wrong section).
%include "weave/wblob.inc"          ; reading the bundle claim
%include "weave/wdraw.inc"          ; the paint core (WEAVE-SPEC 1.2's seam)

    CC_IMAGE_END                    ; cc_bss_end, cc_modc_end and cc_image_end
