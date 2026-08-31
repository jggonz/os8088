; =============================================================================
; os8088 - apps/loom/loom.asm
;
; The assembly shim of LOOM, the in-OS IDE for the Weave family (WEAVE-SPEC
; 1.2, wave 6). It is the top-level nasm source: the .c files are never
; assembled on their own, because `nasm -f bin` has no notion of an external
; symbol, so the compiled C, the runtime, the hand-written cores and the
; 32-byte header are ONE assembly (SPEC.md 73.1).
;
; A shim does four things and nothing else belongs in it: name the package,
; declare which callbacks the C actually defines, include the runtime and then
; the compiled C in that order (the header has to be at file offset 0), and
; close the image. The two halves cannot drift silently - a %define with no C
; function behind it is an nasm external-reference error naming that function,
; and a C function with no %define is code the kernel never calls.
;
; IT IS NOT WEAVE. WEAVE.O88 is a separate package (WEAVE-SPEC 1.2) - the
; WORD/CWORD precedent, that two things may not answer to one name - and
; nothing here may reach a `weave` PACKAGE name. What the two share they share
; as SOURCE, which is a different thing entirely and is the reason
; apps/weave/wblob.inc is %included below: SPEC.md 20.5.1 is this platform's
; only code-sharing mechanism, and WEAVE-SPEC 1.2's rule is "never a second
; copy".
; =============================================================================

; --- os88ui.inc's FEATURE DEFINES, AND THEY BELONG AT THE TOP ----------------
; Preprocessor tests are answered in FILE ORDER, and os88ui.inc's own include
; is further down this file. A %define placed beside that include is BELOW
; every %ifdef that reads it: the guarded bodies assemble anyway - the binary
; GROWS - while each of the file's own guarded blocks silently vanishes, which
; on WEAVE measured 405 bytes and a thumb that does not move (SPEC.md
; 13.10.7.4). So they are here, at the top, and the include is below, and the
; distance between the two is the whole point.
%define OS88UI_SCROLL               ; os88ui_sbar/sbhit/sbmove and the
                                    ; OS88UI_SB* answers - the editor's
                                    ; vertical bar
%define OS88UI_ARM                  ; the press/release word. os88ui.inc
                                    ; defines it for every package anyway
                                    ; (see its own %elifndef OS88UI_BARONLY);
                                    ; it is named here because the ALERT below
                                    ; is built on it and a reader should not
                                    ; have to find that out from the other
                                    ; file
%define OS88UI_ALERT                ; SPEC.md 75.3's alert, which is the CLOSE
                                    ; GUARD's Save / Discard / Cancel
                                    ; (SPEC.md 75, OS88UI_ASAVE). 607 bytes of
                                    ; OUR image and none of the kernel's
                                    ;
                                    ; NOT %define OS88UI_SBDRAG: nothing drags
                                    ; a thumb yet, and the state it declares
                                    ; is six bytes of bss and a body. Add it
                                    ; here, not beside the include.

%define CC_PKG_NAME 'LOOM'          ; <= 15 chars, and the stem of LOOM.OVL:
                                    ; crt0.asm builds the module's file name
                                    ; out of this and '.OVL', so the disk and
                                    ; the loader cannot disagree about it

%define CC_HAS_ONKEY                ; void os88_onkey(int, int, void *) - the
                                    ; editor, and WEAVE-SPEC 1.7's shortcuts
                                    ; read as CONTROL CHARACTERS rather than
                                    ; as kernel accelerators (there is no such
                                    ; thing: OSAPI_MENU_SET draws and tracks a
                                    ; bar and nothing else)
%define CC_HAS_ONCLICK              ; void os88_onclick(int, int, void *) -
                                    ; the sidebar rows, the scroll bar and the
                                    ; caret placement
%define CC_HAS_ONRESIZE             ; void os88_onresize(int, int, void *) -
                                    ; SPEC.md 11.98, and NOT the same thing as
                                    ; WM_ONSIZE: that one is asked BEFORE a
                                    ; resize and takes an answer, this one is
                                    ; told after and must not draw
%define CC_HAS_MENUS                ; void os88_oncmd(int, int, void *)
%define CC_HAS_ABOUT                ; void os88_about(void *)
%define CC_HAS_FDLG                 ; void os88_onfile(...) - File > Open and
                                    ; File > New Project..., which are the two
                                    ; halves of WEAVE-SPEC 1.5's second route
%define CC_HAS_ONCLOSE              ; int os88_onclose(void *) - SPEC.md 75.1's
                                    ; negotiator. A modified source is the one
                                    ; thing in this program that cannot be
                                    ; recovered after the window is gone, so
                                    ; every door out asks first (75.3's
                                    ; OS88UI_ASAVE) and os88_wm_close()
                                    ; finishes the job once it is answered
%define CC_HAS_OVL                  ; ...and LOOM.OVL (SPEC.md 73.14), whose
                                    ; tenants are the compilers and the bundle
                                    ; writer (WEAVE-SPEC 1.2). Pack is a menu
                                    ; command and menu commands may refuse,
                                    ; which is the canonical tenant

                                    ; NOT CC_HAS_ONMOUSEUP: nothing in this
                                    ; wave is a control that fires on the
                                    ; RELEASE. A sidebar row and a scroll-bar
                                    ; arrow both act on the PRESS, the way the
                                    ; Finder's rows and every scroll bar in
                                    ; this system do, so there is no armed
                                    ; control to fire. The alert's buttons DO
                                    ; fire on the release and install their
                                    ; own W_ONMOUSEUP on the alert's own
                                    ; window (os88ui.inc's os88ui_ask), which
                                    ; is a different window from ours.
                                    ;
                                    ; NOT CC_HAS_ONTIMER: the caret does not
                                    ; blink. SPEC.md 13.9 gives a window one
                                    ; one-shot and a blink spends it, and on
                                    ; the target machine a blink is two XOR
                                    ; rects every half second for ever - the
                                    ; whole day, whether or not anybody is
                                    ; typing. A static caret says the same
                                    ; thing for nothing.
                                    ;
                                    ; NOT CC_HAS_WORKER: SPEC.md 20.6 rule 7
                                    ; forbids a worker touching a file and
                                    ; SPEC.md 73.14's loader refuses one an
                                    ; overlay, which between them rule out
                                    ; every long job this program has.

%define CC_ICON  "loom/icon.inc"    ; 32 dw rows exactly: 16 mask, 16 data
%define CC_ASSOC "loom/lmassoc.inc" ; 'WML' and 'WJS' - so a source opens on
                                    ; the FIRST double-click, with no prior
                                    ; run and no search (SPEC.md 54.6,
                                    ; WEAVE-SPEC 1.5 step 2)

%include "cc/crt0.asm"              ; the sections, the 32-byte header, the
                                    ; icon and association blocks, the entry
                                    ; and callback trampolines, the overlay
                                    ; runtime and the whole API bridge

%include "loom.gen.asm"             ; the compiled C, found through -I build/

; --- THE SHARED CONTROL LIBRARY, BEFORE THE CORES THAT NAME ITS CONSTANTS ----
; os88ui.inc is the standard control (SPEC.md 20.5.1). It is included HERE -
; after crt0.asm, before our own cores - and apps/weave/weave.asm's header
; carries the whole argument for why that is not the violation it looks like:
; the "at the END of your code" convention in os88ui.inc's own header protects
; the ICON's fixed offset, and crt0.asm emits the icon itself and carries the
; assertion inside it, so nothing placed after that %include can violate it.
;
; What the convention DOES bind is narrower and is obeyed: our own %ifdef
; gates and any %assign naming an os88ui constant sit below this line. The
; feature defines at the top of this file are the other half of that rule -
; they must be ABOVE, and they are.
%include "os88ui.inc"

; --- the hand-written cores (SPEC.md 73.11: the inner loop is assembly) ------
; AFTER the compiled C, because the C names them and nasm resolves a forward
; reference to a label but not a section that has not been opened yet. Their
; own headers carry the segment rules; both leave DS, ES and DF as they found
; them, and both switch back to .text before they end (CLAUDE.md's section
; discipline - the next %include otherwise lands in the wrong section).
%include "weave/wblob.inc"          ; WEAVE-SPEC 1.2'S SHARING RULE, AND THE
                                    ; ONLY MECHANISM THERE IS (SPEC.md
                                    ; 20.5.1). The three claims this program
                                    ; holds - the sources, the compilers'
                                    ; scratch workspace and the output image -
                                    ; are read and written through the SAME
                                    ; w_b/w_w/w_pb/w_pw/w_copy/w_pcopy WEAVE
                                    ; reads a bundle with. apps/loom/loom.h's
                                    ; lm_wb/lm_ob/lm_sb are thin C wrappers
                                    ; over them and never a second copy: two
                                    ; readers of one claim that can disagree
                                    ; about endianness is exactly the failure
                                    ; WEAVE-SPEC 11's byte-identity rule
                                    ; exists to prevent, said about code
%include "weave/wnum.inc"           ; ...and 5.2's 16.16 fraction helper, which
                                    ; apps/weave/wfxc.c names and which
                                    ; apps/loom/lmsheet.c #includes wfxc.c to
                                    ; get. SHARED SOURCE again (WEAVE-SPEC
                                    ; 1.2): the FX pre-compiler in LOOM.OVL
                                    ; and the resident formula compiler in
                                    ; WEAVE are ONE FILE, so a formula typed
                                    ; into a grid and a formula packed into a
                                    ; bundle cannot round differently.
                                    ;
                                    ; NOT BESIDE weave/wfx.inc, because this
                                    ; image does not have one: wfx.inc is the
                                    ; formula VM and LOOM never RUNS a
                                    ; formula, it only compiles them. wfx.inc
                                    ; %includes this file itself, so a shim
                                    ; that has both must not name this one -
                                    ; and this shim does not have both.
%include "loom/lmui.inc"            ; ...and LOOM's own: the alert bridge, the
                                    ; scroll bar, the line scanner and the
                                    ; two claim movers wblob.inc has no reason
                                    ; to carry

; --- THE DRIFT GUARDS, and they are %if and not a comment --------------------
; loom.h carries C copies of two of os88ui.inc's own numbers, because a C file
; may not name an nasm equ. A copy that goes stale is the class of defect this
; whole tree writes guards for, and here it would be silent: the alert would
; be raised with the wrong button set and the close guard would offer OK where
; it meant Save / Discard / Cancel.
%if OS88UI_ASAVE != 2 || OS88UI_ACANCEL != 0FFh
  %error "os88ui.inc's alert sets moved; LM_ASAVE/LM_ACANCEL in lmui.inc follow"
%endif
%if OS88UI_SBNONE != 0 || OS88UI_SBUP != 1 || OS88UI_SBDOWN != 2
  %error "os88ui.inc's OS88UI_SB* renumbered; LM_SB_* in apps/loom/loom.c follow"
%endif
%if OS88UI_SBPGUP != 3 || OS88UI_SBPGDN != 4 || OS88UI_SBTHUMB != 5
  %error "os88ui.inc's OS88UI_SB* renumbered; LM_SB_* in apps/loom/loom.c follow"
%endif

    CC_IMAGE_END                    ; cc_bss_end, cc_modc_end and cc_image_end
