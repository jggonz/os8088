; Protected-mode optional glyph provider across a genuine FSXF_REAL borrow.
; r runs one bracket; subsequent keys are recorded for the host. Entry
; refuses outside protected mode, so SMSW below is reached only on286+.
%include "os88api.inc"

    OS88_HEADER 'KBDREAL', kr_entry, 0, OS88_STACK_192 | OS88_HIGH | OS88_HIGHMEM

kr_entry:
    call OSAPI_CPU_INFO
    test ah, CPU_F_PM
    jz .no
    mov si, kr_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [kr_win], bx
    OS88_REGION_MOVABLE
.out:
    ret
.no:
    stc
    ret

kr_key:
    mov [kr_keybyte], al
    cmp al, 'r'
    jne .out
    cmp byte [kr_started], 0
    jne .out
    mov byte [kr_started], 1
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    call OSAPI_CPU_INFO
    mov [kr_pm0], ah
    mov ax, (KBDL_ES << 8) | KBDV_SET
    call OSAPI_KEYBOARD
    mov [kr_select], ax
    jc .select_failed
    mov di, kr_before
    call kr_copy_a
    mov ax, kr_real
    mov bx, [kr_win]
    mov cx, FSXF_REAL
    xor si, si                  ; FSXR_REAL requested, BP0: no carried claims
    xor bp, bp
    call OSAPI_FSX_RUN
    mov [kr_why], si             ; MOV preserves the returned carry
    pushf
    pop ax
    and al, 1
    mov [kr_cf], al
    call OSAPI_CPU_INFO
    mov [kr_pm1], ah
    mov di, kr_after
    call kr_copy_a
    jmp short .done
.select_failed:
    mov [kr_error], ax
.done:
    mov byte [kr_done], 1        ; publish only after all three records exist
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.out:
    ret

; DS is the bridge's writable REAL paragraph, possibly a copy of our high
; region. Never bank a protected-mode data selector for use in this callback.
kr_real:
    mov byte [kr_ran], 1
    mov [kr_realcs], cs
    cpu 286
    smsw ax                     ; independent proof: actual CR0.PE is clear
    cpu 8086
    mov [kr_msw], ax
    call OSAPI_CPU_INFO
    mov [kr_rmfeat], ah
    mov di, kr_inside
    call kr_copy_a
    ret

; DI -> {bounds word, stride word, A's eight bytes}, in our writable DS.
; FONT_GLYPHS returns a fresh selector in PM, a real segment in REAL. Read
; through ES=DX, keep DS unchanged, and override BP's default stack segment.
kr_copy_a:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov bp, di
    call OSAPI_FONT_GLYPHS
    mov [ds:bp], ax
    mov [ds:bp+2], cx
    cmp cx, 8
    jne .out
    cmp al, 'A'
    ja .out
    cmp ah, 'A'
    jb .out
    xor ah, ah
    mov bx, 'A'
    sub bx, ax
    shl bx, 1
    shl bx, 1
    shl bx, 1
    add si, bx
    mov es, dx
    lea di, [bp+4]
    mov cx, 8
.copy:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    loop .copy
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

kr_tpl:
    dw 150, 70, 230, 64, kr_title, kr_cls
    OS88_CLASS kr_cls, 0, ONKEY, kr_key
kr_title: db 'Keyboard REAL glyphs', 0

    OS88_BSS 58
    OS88_IMAGE_END
kr_select  equ os88_image_end + 0 ; successful AX0102: Spanish provider active
kr_cf      equ os88_image_end + 2
kr_done    equ os88_image_end + 3
kr_why     equ os88_image_end + 4
kr_error   equ os88_image_end + 6
kr_pm0     equ os88_image_end + 8
kr_pm1     equ os88_image_end + 9
kr_rmfeat  equ os88_image_end + 10
kr_ran     equ os88_image_end + 11
kr_msw     equ os88_image_end + 12
kr_realcs  equ os88_image_end + 14
kr_keybyte equ os88_image_end + 16
kr_before  equ os88_image_end + 18
kr_inside  equ os88_image_end + 30
kr_after   equ os88_image_end + 42
kr_win     equ os88_image_end + 54
kr_started equ os88_image_end + 56
