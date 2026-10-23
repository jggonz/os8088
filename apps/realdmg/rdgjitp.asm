; =============================================================================
; rdgjitp.asm - RealDMG's recompiler as a PART of REALDMG.O88 (REALDMG-SPEC
; 16.9): rdgjit.inc's code and its templates, assembled on their own at org 0
; and read by the package into the tail of the recompiler's own claim
; (rw_jitpart) - so an 8088, which never translates, never holds them, and a
; 286 holds them only while it has a cache.
;
; The package far-calls the four entries below, DS = the view. Inside, CS is
; this part's segment, which is all rdgjit.inc's `[cs:rt_tab + ...]` needs.
; The core reaches rdg_jit through [S_JITVEC], which rt_init points here; and
; rdg_jit leaves through this file's own rdg_ret, rdgsys.inc's, reading the view
; from this file's own word. The bare harness (tests/rdgcore/rdghost.asm) still
; includes rdgjit.inc inline, so the same source serves both.
; =============================================================================
cpu 8086
bits 16
org 0

%include "rdgdefs.inc"

; --- the entries: far, each `call` and `retf` (offsets fixed: RWJ_* in
; realdmg.asm) --------------------------------------------------------------
pt_init:                                ; 0: rt_init (AX, BX, CX, DL as its)
    mov [cs:rdg_viewseg], ds
    call rt_init
    retf
    times 10-($-$$) nop
pt_resume:                              ; 10: rt_resume
    call rt_resume
    retf
    times 20-($-$$) nop
pt_preload:                             ; 20: rt_preload, AX = the count
    call rt_preload
    retf
    times 30-($-$$) nop
pt_dispatch:                            ; 30: rt_dispatch, AL as its
    call rt_dispatch
    retf

rdg_viewseg  dw 0                        ; the view, for RDG_TOHOST and rdg_ret

; RDG_TOHOST and rdg_ret as rdgsys.inc has them, over this part's own word
%macro RDG_TOHOST 0
    mov [ss:S_SAVDS],ds
    cli
    mov [ss:S_SP],sp
    mov sp,[ss:S_HSP]
    mov ss,[ss:S_HSS]
    sti
    mov ds,[cs:rdg_viewseg]
    cld
%endmacro

rdg_ret:
    cli
    mov ss,[cs:rdg_viewseg]
    mov sp,[ss:S_SP]
    mov ds,[ss:S_SAVDS]
    xor dl,dl
    jmp far [ss:S_RETFAR]

%include "rdgjit.inc"
