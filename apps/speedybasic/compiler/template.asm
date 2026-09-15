; Fixed runtime image for packages produced inside os8088.  The compiler
; copies SPEEDYCC.RT, replaces the code hole below, patches its title/header,
; and writes the result as the user's .O88 file.

%define CC_PKG_NAME 'BASICTPL'
%define CC_HAS_ONKEY
%define CC_HAS_ONRESIZE
%define CC_HAS_ONWAKE
%define CC_HAS_ONTIMER
%define CC_ICON "speedybasic/icon.inc"

%include "cc/crt0.asm"
%include "speedycc.gen.asm"
%include "speedybasic/compiler/gfx.inc"
%define SBN_FLAT
%include "speedybasic/sbnum.inc"
%include "speedybasic/sbmem.inc"

; Generated programs keep resumable state and numeric scalars in a fixed BSS
; bank.  Their instructions address these offsets directly, so changing this
; layout requires regenerating and validating the compiler ABI constants.
section .bss
_sbaot_pc:       resw 1
_sbaot_vlo:      resw 64
_sbaot_vhi:      resw 64
_sbaot_vkind:    resw 64

; A direct 8086 backend patches this fixed-size slot.  The default body is a
; valid program that immediately reports SBR_DONE, keeping the template itself
; runnable and making an unpatched/corrupt build harmless.
section .text
_sbp_program:
_sbaot_code:
    mov ax, 1
    ret
    times 8192 - ($ - _sbaot_code) db 0x90
_sbaot_code_end:

    CC_IMAGE_END
