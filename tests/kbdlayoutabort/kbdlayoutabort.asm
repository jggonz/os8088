; Failed-entry glyph ownership fixture, SPEC.md 9.16/21.
; Default: a valid entry acquires HOLD, then deliberately returns CF=1.
; -DKA_LIVE: the same entry acquires HOLD and creates a closable window.
; The live arm records QUERY before HOLD; h/r repeat HOLD/RELEASE so the
; host can prove that a reused instance slot has independent ownership.
%include "os88api.inc"

%ifdef KA_LIVE
    OS88_HEADER 'KBDHOLD', ka_entry, 0, OS88_HIGH | OS88_HIGHMEM
%else
    OS88_HEADER 'KBDABORT', ka_entry, 0, OS88_HIGH | OS88_HIGHMEM
%endif

ka_entry:
    xor ax, ax
    call OSAPI_KEYBOARD
    mov [ka_before], ax
    mov al, KBDV_HOLD
    call ka_control
    cmp byte [ka_error], 0
    jne ka_abort
%ifdef KA_LIVE
    mov si, ka_tpl
    call OSAPI_WM_CREATE
    jc ka_abort
    OS88_REGION_MOVABLE
    clc
    ret
%endif
ka_abort:
    stc                         ; deliberately refuse after acquiring a grant
    ret

ka_control:
    call OSAPI_KEYBOARD
    mov [ka_after], ax
    mov byte [ka_error], 0       ; MOV keeps the call's carry
    jnc .done
    inc byte [ka_error]
.done:
    inc byte [ka_serial]
    ret

%ifdef KA_LIVE
ka_key:
    push ax
    cmp al, 'h'
    je .hold
    cmp al, 'r'
    jne .done
    mov al, KBDV_RELEASE
    jmp short .call
.hold:
    mov al, KBDV_HOLD
.call:
    call ka_control
.done:
    pop ax
    ret

ka_tpl:
    dw 160, 70, 200, 60, ka_title, ka_cls
    OS88_CLASS ka_cls, 0, ONKEY, ka_key
ka_title: db 'Keyboard hold fixture', 0
%endif

    OS88_BSS 6
    OS88_IMAGE_END
ka_before equ os88_image_end + 0 ; word: QUERY before the first HOLD
ka_after  equ os88_image_end + 2 ; word: most recent control result
ka_error  equ os88_image_end + 4 ; byte: control refused
ka_serial equ os88_image_end + 5 ; byte: control calls completed
