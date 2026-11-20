; Optional CP850 face, SPEC.md 6.7 / 9.16.3. Included in KEYBOARD.DRV.
; Entry DS = kernel, ES = the module's writable data alias (also on mega).
; All state/data stays in this module. No memory claim or file read here.

section .modq
klf_enable:
    push ax
    push bx
    push cx
    push si
    push di
    push ds
    call klf_lock
    cmp byte [es:klf_active], 0
    jne .done
    mov ax, [font_seg]
    mov [es:klf_saved_seg], ax
    mov si, [font_base]
    mov [es:klf_saved_base], si
    mov ds, ax
    mov di, klf_table
    mov cx, FONT_BYTES / 2
    cld
    rep movsw                       ; retain the selected ASCII face
    pop ds
    push ds                         ; the saved kernel DS, needed below
    pushf
    cli
    mov ax, es
    mov [font_seg], ax
    mov word [font_base], klf_table
    mov word [font_last], (223 << 8) | 255
    mov byte [es:klf_active], 1       ; publish only a complete table
    popf
.done:
    call klf_unlock
    pop ds
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    clc
    ret

klf_disable:
    push ax
    push bx
    call klf_lock
    cmp byte [es:klf_active], 0
    je .done
    cmp byte [es:klf_active], 2
    je .suspended
    pushf
    cli
    mov ax, [es:klf_saved_seg]
    mov [font_seg], ax
    mov ax, [es:klf_saved_base]
    mov [font_base], ax
    mov word [font_last], ((FONT_LAST-FONT_FIRST) << 8) | FONT_LAST
    mov byte [es:klf_active], 0
    popf
    jmp short .done
.suspended:
    mov byte [es:klf_active], 0
.done:
    call klf_unlock
    pop bx
    pop ax
    clc
    ret

; Private wake bracket: 2 means a live provider temporarily unbound. The
; existing byte retains this fact; layout/client pins belong to keyboard.inc.
klf_suspend:
    cmp byte [es:klf_active], 1
    jne .done
    call klf_disable
    mov byte [es:klf_active], 2
.done:
    clc
    ret
klf_rearm:
    cmp byte [es:klf_active], 2
    jne .done
    mov byte [es:klf_active], 0
    jmp klf_enable              ; sample the repaired original pointer afresh
.done:
    clc
    ret

; A worker can be paused between reading font_base and font_seg. Publishing
; the pair with IF=0 is not enough: serialize with the whole drawing burst.
; Calls from a window callback already own this non-reentrant mutex.
; BX = whether THIS helper acquired it; outer helpers save the caller's BX.
klf_lock:
    xor bx, bx
    cmp byte [gfx_lock_flag], 0
    je .take
    mov bl, [sch_cur]
    cmp bl, [gfx_lock_own]
    je .owned
.take:
    call KERNEL_SEG:cw_gfx_lock
    mov bl, 1
    ret
.owned:
    xor bx, bx
    ret
klf_unlock:
    or bl, bl
    jz .done
    call KERNEL_SEG:cw_gfx_unlock
.done:
    ret

; One writable table in the module image: ASCII is initialized on first
; use; the generated extended bytes already occupy their final positions.
; Keeping a second immutable 1KB copy would spend a second KB of active RAM.
klf_table:
    times FONT_BYTES db 0
    times 8 db 0                    ; DEL is never a printable glyph
%include "keyfont-data.inc"
%if klf_cp850 - klf_table != (128 - FONT_FIRST) * 8
  %error "CP850 rows must follow the ASCII face and DEL"
%endif

section .modqb
klf_active:     resb 1
klf_saved_seg:  resw 1
klf_saved_base: resw 1
section .text
