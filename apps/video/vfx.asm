; =============================================================================
; os8088 - apps/video/vfx.asm
;
; THE VIDEO PLAYER'S SPEAKER SHAPER, A PART (SPEC.md 98.3.15.2): part 2 of
; the kern_mega arm's VIDEO.O88 (OP_SEG | OP_COMP | OP_LAZY), fetched the
; first time a clip made for a card plays on the speaker, and far-called
; through video/vfx.inc's vectors. It is apps/os88spkfx.inc, assembled ALONE:
; what the include reads of the speaker library (os88spk.inc's ring, its
; table and whether it drives a Covox) arrives in registers at VFXV_INIT and
; is kept here, under os88spk.inc's own names; what it writes there
; (os88spk_sil) goes back in CL.
;
; Set up in protected mode - the arm runs on kern_mega - and the include's
; own PKG_MEGA arm is what makes a span cost no trap: os88spkfx_init banks
; DS, which VFXV_INIT makes this part's DATA segment, as the selector its
; writing copies go through (SPEC.md 34.11.1.1). Init's own writes go
; through CS and are a #GP each that the kernel completes - once a play, where
; the include has always paid them. A REAL-MODE bracket (98.3.24) plays the
; same writing copies through a real segment: VFXV_BIND gives the part its
; data segment and the ring's in the mode the call comes from.
; =============================================================================
    cpu 8086
    bits 16
    org 0

%include "os88api.inc"               ; (assembled -DPKG_MEGA: the Makefile)
%include "video/vfx.inc"

    dw VFX_SIGVAL                   ; +0
    db VFX_ABIVER                   ; +2
    db VFXV_N                       ; +3
    dw vfx_init                     ; +4 VFXV_INIT
    dw vfx_level                    ; +6 VFXV_LEVEL
    dw vfx_emit                     ; +8 VFXV_EMIT
    dw vfx_bind                     ; +10 VFXV_BIND

vfx_init:
    push ds
    mov ds, bx                      ; (our data: os88spkfx_pmset banks it)
    mov [os88spk_lptp], cx
    mov [os88spk_rseg], dx
    mov [os88spk_rl], si
    xchg ax, bp                     ; (its count as the library has it,
    mov [os88spk_sil], al           ;  unchanged unless the shaper says
    xchg ax, bp                     ;  otherwise)
    call os88spkfx_init
    mov cl, [os88spk_sil]
    pop ds
    retf

vfx_level:
    call os88spkfx_level
    retf

vfx_emit:
    call os88spkfx_emit
    retf

vfx_bind:                           ; (98.3.24: a bracket's mode change)
    push ds
    mov ds, bx
    mov [os88spk_rseg], dx
    mov [os88spkfx_dsel], bx
    pop ds
    retf

; what os88spkfx.inc reads of the speaker library, as VFXV_INIT handed it
os88spk_lptp:   dw 0
os88spk_rseg:   dw 0
os88spk_rl:     dw 0
os88spk_sil:    db 0x80

%include "os88spkfx.inc"

%if SPKFX_NLEV != VFX_NLEV
  %error "video/vfx.inc's VFX_NLEV is not os88spkfx_t.inc's SPKFX_NLEV"
%endif
%if SPKFX_RINGEND != VFX_RINGEND
  %error "video/vfx.inc's VFX_RINGEND is not os88spkfx.inc's"
%endif
