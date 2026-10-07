; =============================================================================
; os8088 - tests/spkfx/spkfx.asm
;
; SPKFX: apps/os88spkfx.inc run over bytes the HARNESS chose, so its output
; can be compared with tools/os88spkfx.py's to the byte and its cycles timed.
; NEVER shipped: tests/spkfx.py assembles it into a scratch floppy.
;
; On open it claims the ring (os88spk.inc's table lives there), an input
; buffer and an output buffer, and publishes their segments at fx_rseg,
; fx_iseg, fx_oseg; fx_up = 1 says so. The harness then writes the input,
; the parameters (fx_rate, fx_pre, fx_idle, fx_rat, fx_len, fx_span, fx_split) and
; presses 'g': os88spk_init, os88spkfx_init, then for each span of fx_span
; samples os88spkfx_level and os88spkfx_emit - in two pieces, the first
; fx_split long, when fx_split is not 0 - and fx_done counts the runs.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'SPKFX', fx_entry

FX_KB   equ 32                      ; input and output, each

fx_entry:
    push ax
    push dx
    push si
    mov si, fx_tpl
    call OSAPI_WM_CREATE
    mov ax, 5                       ; the ring: 4 KB and the table after it
    call OSAPI_MEM_CLAIM
    jc .out
    mov [fx_rseg], dx
    mov ax, FX_KB
    call OSAPI_MEM_CLAIM
    jc .out
    mov [fx_iseg], dx
    mov ax, FX_KB
    call OSAPI_MEM_CLAIM
    jc .out
    mov [fx_oseg], dx
    mov byte [fx_up], 1
.out:
    pop si
    pop dx
    pop ax
    ret

fx_paint:
    ret

fx_onkey:
    cmp al, 'g'
    jne .ret
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    push es
    mov di, [fx_rseg]
    xor ah, ah                      ; size code 0: RL = 4096
    mov dx, [fx_rate]
    xor cl, cl
    call os88spk_init
    jc .fail
    mov di, fx_fam
    mov al, [fx_pre]
    mov ah, [fx_idle]
    call os88spkfx_init
    mov al, [fx_rat]                ; 0, or the ratchet's start level + 1
    or al, al                       ; (SPEC.md 34.11.9.1)
    jz .nr
    dec ax
    call os88spkfx_ratchet
.nr:
    mov bx, [fx_len]
    mov dx, [fx_split]
    mov es, [fx_oseg]
    mov ds, [cs:fx_iseg]
    xor si, si
    xor di, di
.span:
    or bx, bx
    jz .done
    mov cx, [cs:fx_span]            ; this span: min(span, left)
    cmp cx, bx
    jbe .n
    mov cx, bx
.n:
    sub bx, cx
    call os88spkfx_level
    or dx, dx
    jz .whole
    cmp dx, cx
    jae .whole
    push cx                         ; two pieces: the split, then the rest
    mov cx, dx
    call os88spkfx_emit
    pop cx
    sub cx, dx
.whole:
    call os88spkfx_emit
    jmp short .span
.done:
    inc byte [cs:fx_done]
.fail:
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.ret:
    ret

%include "os88spk.inc"
%include "os88spkfx.inc"

fx_tpl:
    dw 40, 40, 160, 40
    dw fx_ttl, fx_paint, fx_onkey, 0
fx_ttl: db 'SpkFx', 0

fx_rate:  dw 8000
fx_len:   dw 0
fx_span:  dw 256
fx_split: dw 0
fx_pre:   db 1
fx_idle:  db 1
fx_rat:   db 0                      ; 0: the leveller; n: the ratchet from n - 1
fx_up:    db 0
fx_done:  db 0
fx_rseg:  dw 0
fx_iseg:  dw 0
fx_oseg:  dw 0

    OS88_BSS SPKFX_NLEV * 256
    OS88_IMAGE_END

fx_fam equ os88_image_end
