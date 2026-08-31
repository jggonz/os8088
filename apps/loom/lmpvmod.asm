; =============================================================================
; os8088 - apps/loom/lmpvmod.asm             LOOM.WPV (WEAVE-SPEC 1.2.4)
;
; The preview module: WEAVE-SPEC 7's flow walk and WEAVE-SPEC 6's component
; painter, in a SECOND RESIDENT SEGMENT beside LOOM.O88 - read once when
; Preview is first opened, held until the instance closes, far-called from
; LOOM's own W_PAINT.
;
; IT IS wcanvas.asm's SHAPE ONE WAVE LATER, and the difference is that this
; module is compiled C rather than hand-written assembly:
;
;   * an OVERLAY (SPEC.md 73.14) moves CODE and leaves every global, literal
;     and bss byte resident and DS-relative. What does not fit in LOOM is the
;     DATA - the walk's 2,500-byte layout table and the painter's six tables
;     keyed by comp_id, ~4.7KB against the 594 bytes wave 6 closed with
;     (WEAVE-SPEC 1.7.1). An overlay cannot move any of it, so the overlay is
;     not full here, it is the wrong instrument.
;   * a SECOND SEGMENT has a DS of its own, so the data costs LOOM one dword
;     in apps/loom/lmpv.inc and a claim - and nothing at all until the first
;     time somebody opens the pane.
;
; docs/WEAVE-PLAN.md 2.10 prices the alternatives this beat, and the first
; paragraph of the pull request names it as the decision the owner may
; reverse.
;
; -----------------------------------------------------------------------------
; IT IS A SEPARATE ASSEMBLY, AND THAT IS WHAT MAKES THE ABI A CONTRACT
; -----------------------------------------------------------------------------
; `nasm -f bin` has no notion of an external symbol, so this file and
; apps/loom/loom.asm share exactly one thing: apps/weave/wpvabi.inc. Nothing
; here may name a package label and nothing there may name one of ours. The
; three-word stamp at +0 (magic, ABI, size) is what a load checks before it
; believes a byte of this file (WEAVE-SPEC 1.2.4).
;
; -----------------------------------------------------------------------------
; THE SEGMENT RULES, WHICH ARE THE WHOLE TAX ON LIVING OUT HERE
; -----------------------------------------------------------------------------
;  * DS is the CALLER's on entry and on return. The entry banks it, sets
;    DS = CS - so every static in the compiled C is a plain DS-relative name
;    and the C is ordinary C - and puts it back.
;  * ES is preserved across the verb. Inside it, ES is whatever the primitives
;    and the cores want it to be.
;  * SS is LOOM's task stack, never ours: [bp+n] addresses the STACK and is
;    right for a C frame and wrong for anything of ours (CLAUDE.md's hard
;    rule). tools/cc8086.py's refusal of `&local` binds this compilation
;    exactly as it binds a package's, and the Makefile runs it on this file's
;    output for that reason.
;  * OSAPI_* are KERNEL_SEG:offset far immediates, so the module calls the
;    kernel directly and needs no call-back vector into the package - the same
;    sentence apps/weave/wcanvas.asm makes about WEAVE.WSM.
;  * cpu 8086, and the compiled C is put through tools/cc8086.py first, which
;    is where that is enforced.
; =============================================================================

cpu 8086
bits 16

; --- os88ui.inc's FEATURE DEFINES, AND THEY BELONG AT THE TOP -----------------
; The same three apps/weave/weave.asm names and for the same reasons, because
; the painter this module carries IS that package's painter: a <list> and a
; <grid> want the scroll bar, a <button>/<check>/<radio> wants the arm word
; (wpaint.c's w_onhit path names os88ui_arm through wd_arm even though nothing
; in this module can reach it), and wui.inc is NOT here so the alert is not.
;
; Preprocessor tests are answered in FILE ORDER and os88ui.inc's include is at
; the bottom of this file, which is the distance weave.asm's own note is about.
%define OS88UI_SCROLL
%define OS88UI_ARM

; =============================================================================
; THE SECTIONS
;
; apps/cc/crt0.asm's four, in its order and for its reasons - `.text start=0`
; so that a label's value IS its file offset, and `.data` padded to even before
; the image end is taken so that `.bss follows=.data align=2` begins exactly
; where the loader stops zeroing. That whole paragraph is in crt0.asm and is
; not repeated here; what IS repeated is the layout, because this module does
; not include crt0.asm (it has no package header, no entry trampoline, no
; callback and no overlay runtime - it is not a package).
; =============================================================================
section .text   start=0
section .rodata follows=.text   align=2
section .data   follows=.rodata align=2
section .bss    follows=.data   align=2 nobits

%include "os88api.inc"
%include "weave/wpvabi.inc"

; =============================================================================
; THE HEADER (WEAVE-SPEC 1.2.4) - eight bytes, then the entry
; =============================================================================
section .text
lpv_hdr:
    dw WPV_MAGIC                    ; +0  so a truncated or unrelated file is
                                    ;     refused rather than entered
    dw WPV_ABI                      ; +2  the contract number, from the ONE
                                    ;     file both assemblies include
    dw LPV_IMAGE_SIZE               ; +4  our own size: the package compares it
                                    ;     with WPV_SIZE, injected by the
                                    ;     Makefile after this file is built and
                                    ;     before the package is
    dw LPV_BSS_SIZE                 ; +6  ...and the bss past it, which the
                                    ;     loader's claim has to cover and which
                                    ;     this module zeroes itself on its
                                    ;     first entry

; ...and the door is at WPV_ENTRY, asserted rather than counted by eye. It is
; `$ - $$` and not `lpv_entry` because nasm will not compare a section-relative
; label to a constant in a `%if` (apps/cc/crt0.asm says so where it makes the
; same assertion about the icon's offset), and a difference taken inside one
; section always resolves. The header above is four words; a fifth without a
; matching WPV_ENTRY would have LOOM far-call into the middle of an
; instruction.
%if ($ - $$) != WPV_ENTRY
  %error "lmpvmod.asm: the entry is not at WPV_ENTRY - the header grew"
%endif

; -----------------------------------------------------------------------------
; lpv_entry - the far-called door, at WPV_ENTRY = 8
;
; in:  AL = the verb (WPVV_*), BX/CX/DX = its arguments
; out: AX = the answer. BP, DS, ES, SI, DI, SS:SP and DF as they arrived.
; -----------------------------------------------------------------------------
lpv_entry:
    push bp
    push si
    push di
    push es
    push ds
    mov [cs:lpv_cdsw], ds           ; the caller's DS, for the verb that reads
                                    ; a parameter block in it
    push cs
    pop ds                          ; ...and now every label below is DS
    cld

    ; --- the bss, zeroed ONCE, by us -----------------------------------------
    ; The loader in apps/loom/lmprev.c claims image + bss and reads image
    ; bytes into it; the tail is whatever the heap last held. A package's bss
    ; is zeroed by the kernel's loader (SPEC.md 21 step 8) and a module's is
    ; nobody's, so it is ours - and doing it here rather than in LOOM keeps
    ; the two extents in the one assembly that knows them.
    ;
    ; `rep stosb` with ES set to OUR segment. CLAUDE.md's rule against stos in
    ; a C package is about ES being the KERNEL's; here ES is pushed above,
    ; loaded deliberately, and popped below.
    ;
    ; lpv_ready is in .data and not .bss, which is the whole trick: a flag in
    ; the region being cleared cannot say whether the region has been cleared.
    cmp word [lpv_ready], 0
    jne .go
    mov word [lpv_ready], 1
    push ax
    push cx
    push di
    push ds
    pop es
    mov di, lpv_bss_beg
    mov cx, LPV_BSS_SIZE
    xor al, al
    rep stosb
    pop di
    pop cx
    pop ax
.go:
    xor ah, ah                      ; AL is the verb; the high byte is not one
    push dx                         ; cdecl, RIGHT TO LEFT (SPEC.md 73.3):
    push cx                         ;   arg 4 void *win   = DX
    push bx                         ;   arg 3 unsigned parm = CX
    push ax                         ;   arg 2 unsigned bseg = BX
    call _lpv_verb                  ;   arg 1 int verb      = AL
    add sp, 8
    pop ds
    pop es
    pop di
    pop si
    pop bp
    retf

; -----------------------------------------------------------------------------
; unsigned lpv_cds(void) - the CALLER's DS, banked by the entry above.
;
; WPVV_PAINT's parameter block lives in it (WEAVE-SPEC 1.2.4), and the C reads
; that block with wblob.inc's w_b/w_w against this segment - the same explicit
; (segment, offset) pair every other cross-segment read in this family uses,
; and never a C pointer (C64-SPEC 3.6).
; -----------------------------------------------------------------------------
_lpv_cds:
    mov ax, [lpv_cdsw]
    ret

; =============================================================================
; DATA AND BSS - BEFORE THE COMPILED C, AND THE ORDER IS LOAD-BEARING
;
; nasm coalesces a section's fragments in EMISSION order, so lpv_bss_beg is
; the first label in .bss only if this block is emitted before the %include
; that brings the C's own .bss in. Put it after, and the entry's `rep stosb`
; clears thirty bytes of runtime state and none of the walk's or the painter's
; tables - which is a module that works perfectly on its first call and reads
; the previous tenant of the heap on its second. apps/cc/crt0.asm has the same
; ordering for the same reason and says so at cc_bss_beg.
;
; .data is never empty either - crt0.asm's note says why a section with no
; content leaves the image-size label one byte past the file - and here
; lpv_ready is what keeps it non-empty as well as being the first-entry flag.
; =============================================================================
section .data
lpv_ready:  dw 0                    ; 0 until the bss below has been zeroed
lpv_cdsw:   dw 0                    ; the caller's DS, re-banked every entry

; -----------------------------------------------------------------------------
; THE THUNK'S OWN STATE, which crt0.asm would have declared.
;
; apps/cc/os88thunk.asm is ONE file and it names six words of package state -
; the wm_create template, the last file error, the file dialog's name buffer,
; the association block, the banked window and the worker's stack top. This
; module calls none of the routines that use them: it creates no window, opens
; no file, declares no association and spawns no worker. They are here because
; a nasm image resolves every name its text mentions, not because anything
; reaches them, and thirty bytes is cheaper than a second copy of the thunk
; with the window and file halves cut out - which would be exactly the "second
; copy" WEAVE-SPEC 1.2 is about, said about the SDK.
;
; If a later wave gives this module a reason to create a window, cc_tpl's last
; three words are the callback vectors and they are 0 here on purpose.
; -----------------------------------------------------------------------------
cc_tpl:     dw 0, 0, 0, 0           ; WT_X, WT_Y, WT_W, WT_H
            dw 0                    ; WT_TITLE
            dw 0, 0, 0              ; WT_PAINT, WT_ONKEY, WT_ONCLICK - none

section .bss
lpv_bss_beg:                        ; FIRST label in .bss: everything below it
                                    ; is zeroed by the entry above, and the
                                    ; compiled C's own bss follows because the
                                    ; C was %included further up
cc_win:     resw 1
cc_ferr:    resw 1
cc_fname:   resb 13
cc_assoc:   resb 11
cc_wksp:    resw 1


section .text

; =============================================================================
; THE API BRIDGE
;
; apps/cc/os88thunk.asm on its own, WITHOUT apps/cc/crt0.asm: the thunk emits
; no section directive of its own and names nothing crt0 declares, so it is
; the one half of the C runtime a module can have. crt0's other half - the
; package header, the entry and callback trampolines, the overlay runtime and
; cc_iswk - is all about being a PACKAGE, and this is not one.
; =============================================================================
%include "cc/os88thunk.asm"

; =============================================================================
; THE COMPILED C
; =============================================================================
%include "lmpvmod.gen.asm"          ; found through -I build/

; =============================================================================
; THE SHARED CONTROL LIBRARY, THEN THE CORES THAT NAME ITS CONSTANTS
;
; The order is apps/weave/weave.asm's, and its note is the authority: wdraw.inc
; evaluates OS88UI_SB* in a `%if` and sizes a `resb` from OS88LINE_SZ, and nasm
; answers both where it sees them. os88line.inc after os88ui.inc because it
; calls into that file.
;
; wui.inc, wvm.inc, wfx.inc and wband.inc are NOT here. The alert, the WJS VM,
; the FX VM and the band composer are the runtime's, and a Preview runs no
; bytecode, recalculates no cell and raises no alert - so they would be bytes
; of a file LOOM has to read off a floppy for a picture that never calls them.
; =============================================================================
%include "os88ui.inc"
%include "os88line.inc"
%include "weave/wblob.inc"          ; reading a claim: the bundle's and the
                                    ; caller's parameter block alike
%include "weave/wdraw.inc"          ; the paint core (WEAVE-SPEC 1.2's seam) -
                                    ; the SAME TEXT WEAVE.O88 assembles, which
                                    ; is the whole reason the two pictures
                                    ; cannot drift

; --- THE DRIFT GUARD, and it is %if and not a comment ------------------------
; apps/weave/weave.h carries a C copy of os88line.inc's block size, because a C
; file may not name an nasm equ. weave.asm asserts it for the package; this
; assembly compiles the same header into a different image and has to assert it
; for itself, or the copy could go stale here and nowhere else.
%if OS88LINE_SZ != 20
  %error "os88line.inc's block moved; wact.c's LNW_* must follow"
%endif

; ...and the module's own half of the ABI guard apps/loom/loom.asm carries.
; apps/weave/wpvabi.h is a C copy of apps/weave/wpvabi.inc and BOTH images
; compile it, so both assert it: a stale copy caught in only one of the two
; would be caught in the image that does not depend on the field.
%if WPV_ABI != 1 || WPV_MAGIC != 0x5057
  %error "wpvabi.inc's stamp moved; apps/weave/wpvabi.h must follow"
%endif
%if WPV_H_MAGIC != 0 || WPV_H_ABI != 2 || WPV_H_SIZE != 4 || WPV_H_BSS != 6
  %error "wpvabi.inc's header moved; apps/weave/wpvabi.h must follow"
%endif
%if WPV_ENTRY != 8 || WPVV_PAINT != 0 || WPVV_ABOUT != 1
  %error "wpvabi.inc's entry or verbs moved; apps/weave/wpvabi.h must follow"
%endif
%if WPVP_X != 0 || WPVP_Y != 2 || WPVP_W != 4 || WPVP_H != 6 || WPVP_CARD != 8
  %error "wpvabi.inc's parameter block moved; apps/weave/wpvabi.h must follow"
%endif
%if WPVE_MAGIC != 1 || WPVE_SECT != 2 || WPVE_CARD != 3 || WPVE_PANE != 4
  %error "wpvabi.inc's refusal codes moved; apps/weave/wpvabi.h must follow"
%endif

; -----------------------------------------------------------------------------
; CLOSING THE IMAGE - crt0.asm's CC_IMAGE_END, open-coded because this module
; does not include crt0.asm. The `align 2` is taken BEFORE the image end for
; exactly the reason written there: without it an odd-length .data leaves the
; last byte of .bss outside the range the zero above clears.
; -----------------------------------------------------------------------------
section .bss
lpv_bss_end:
section .data
align 2, db 0
lpv_image_end:
LPV_IMAGE_SIZE equ lpv_image_end
LPV_BSS_SIZE   equ lpv_bss_end - lpv_bss_beg
section .text
