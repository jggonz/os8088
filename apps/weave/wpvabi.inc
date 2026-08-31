; =============================================================================
; os8088 - apps/weave/wpvabi.inc     THE LOOM.WPV CONTRACT (WEAVE-SPEC 1.2.4)
;
; The ONE file both assemblies %include, and the whole reason the two can be
; trusted to agree about anything: apps/loom/loom.asm builds LOOM.O88 and
; apps/loom/lmpvmod.asm builds LOOM.WPV, and they are separate `nasm -f bin`
; jobs with no linker and no shared symbol between them (SPEC.md 73.1). Every
; number below is therefore a CONTRACT rather than an implementation detail,
; and WPV_ABI is what makes a change to one of them a refused load rather than
; a wrong picture.
;
; IT LIVES IN apps/weave/ AND NOT IN apps/loom/, which looks backwards until
; you read what the module contains: WEAVE's flow walk (WEAVE-SPEC 7) and
; WEAVE's component painter (WEAVE-SPEC 6), compiled a second time into a
; segment LOOM can far-call. The contract belongs beside the code it is a
; contract about, next to apps/weave/wsmabi.inc, which is the same file for
; the same shape of module one wave earlier.
;
; IT IS NOT THE CONTRACT EITHER. docs/WEAVE-SPEC.md is - sections 1.2.4 and
; 1.7.1 - and this file is written from them (the os88.h/os88api.inc rule,
; said a fourth time). Where the two disagree, that one is right.
;
; WHY THERE IS A SECOND MODULE AT ALL, in one paragraph, because the next
; person to read this file will ask: SPEC.md 20.1 caps a package's image + bss
; at APP_MAX_SIZE = 0xF000, a kernel constant a package may not raise, and
; wave 6 closed LOOM at 60,846 of it - 594 bytes spare. The flow walk and the
; painter name ~4.7KB of RESIDENT data between them (WEAVE-SPEC 1.7.1 has the
; arithmetic), and SPEC.md 73.14's overlay moves CODE and leaves every global,
; literal and bss byte resident and DS-relative. So the overlay is not merely
; full here, it is the wrong instrument: it cannot move the thing that does
; not fit. The answer is CLAUDE.md's own hard rule read through WEAVE-SPEC
; 1.2.2's precedent - a second segment, and a RESIDENT one, whose data lives
; in its own 64KB and costs LOOM one dword and a claim.
; =============================================================================

; -----------------------------------------------------------------------------
; THE ABI NUMBER
;
; Bump it for ANY change to a verb, a register contract, or a layout below.
; The package assembles this value in and the module carries it at +2; a
; mismatch refuses the load with a sentence naming the file (WEAVE-SPEC 10.3).
; This is the half `.OVL`'s two size words cannot express - a contract change
; need not change a size - and it is why the stamp here is three words rather
; than two, exactly as wsmabi.inc argues it.
; -----------------------------------------------------------------------------
WPV_ABI     equ 1

WPV_MAGIC   equ 0x5057              ; 'W','P' little-endian

; --- the module's header, at file offset 0 (WEAVE-SPEC 1.2.4) ----------------
;
; IT CARRIES A BSS WORD WHERE WEAVE.WSM CARRIES A STATE OFFSET, and that one
; difference is the whole difference between an assembly module and a C one.
; wcanvas.asm's state is initialised bytes inside its own image, so its claim
; is exactly the file; this module is compiled C with a `.bss` the loader must
; ZERO, so the claim is image + bss and the module cannot be entered before
; those bytes are 0. The module zeroes them ITSELF on its first entry (there
; is a flag in its .data), so the loader's whole job is to make the claim big
; enough - which is what this word is for.
WPV_H_MAGIC equ 0                   ; word: WPV_MAGIC
WPV_H_ABI   equ 2                   ; word: WPV_ABI
WPV_H_SIZE  equ 4                   ; word: the module's own byte count
WPV_H_BSS   equ 6                   ; word: bytes of .bss past the image
WPV_ENTRY   equ 8                   ; ...and the far-called entry point

; --- the verbs (WEAVE-SPEC 1.2.4) --------------------------------------------
; Entered by `call far`, AL = the verb. DS is the caller's on entry and on
; return; the module switches its own and puts it back. ES is preserved. The
; answer is in AX.
WPVV_PAINT  equ 0                   ; BX = the staged bundle's claim segment,
                                    ; CX -> a WPVP_* block in the CALLER's DS,
                                    ; DX = the window (KERNEL_SEG offset).
                                    ; out AX = 1 the card was walked and
                                    ; painted, 0 = the image is not a bundle
                                    ; this module can read (WPVE_* in AH)
WPVV_ABOUT  equ 1                   ; out AX = WPV_ABI. The one verb that
                                    ; needs no bundle, so LOOM's About box can
                                    ; say which module is actually loaded
                                    ; rather than which one it asked for

; --- WPVV_PAINT's parameter block, in the caller's DS ------------------------
; THE PANE, IN SCREEN PIXELS, and it is a rect rather than a window because
; that is the one thing this module cannot ask the window manager for: LOOM's
; Preview is a CHILD AREA inside LOOM's content box (WEAVE-SPEC 1.7), and
; os88_wm_content() would answer with the sidebar and the status row in it.
WPVP_X      equ 0                   ; word: the pane's left edge, screen px
WPVP_Y      equ 2                   ; word: ...its top edge
WPVP_W      equ 4                   ; word: ...its width in px
WPVP_H      equ 6                   ; word: ...and its height
WPVP_CARD   equ 8                   ; word: the card to draw, 1-based;
                                    ; 0 = the bundle's own entry card (2.2)
WPVP_SIZE   equ 10

; --- WPVV_PAINT's refusal codes, returned in AH with AX's low byte 0 ---------
; They are CODES and not sentences, for WEAVE-SPEC 10.5's own reason said
; about a module: a string literal in here is a byte of a file LOOM has to
; read off a floppy, and LOOM already owns every sentence it prints.
WPVE_MAGIC  equ 1                   ; not a .WAB image at all
WPVE_SECT   equ 2                   ; the section table does not fit the image
WPVE_CARD   equ 3                   ; the card asked for is not in the bundle
WPVE_PANE   equ 4                   ; the pane is under one cell wide or tall
