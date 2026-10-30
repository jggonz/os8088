; =============================================================================
; os8088 - tests/megacaps/megacaps.asm
;
; MEGACAPS, a TEST instrument (nothing under tests/ ships): OSAPI_XMEM_CAPS
; ANSWERS WHAT A CLAIM IS THEN GIVEN (docs/plans/KERN-MEGA-PLAN.md 14.122).
; It asks the cell, claims exactly that many KB through OSAPI_XMEM_ALLOC,
; frees it, and claims one KB more - which must be refused, so the answer is
; neither short (a cache counted as used) nor long. tests/megacaps.py starts
; it with the boot's read-ahead store and C:'s FAT window standing in the
; pool. Every result on port E9, one line each:
;
;   MEGACAPS: caps NNNN bl NN   the cell's AX (KB) and BL (free entries)
;   MEGACAPS: got | short EE    the claim of exactly that, or its AX
;   MEGACAPS: over refused | over GOT   one KB more
;   MEGACAPS: done | no NN       the verdict, or where it broke
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'MEGACAPS', mc_entry, 0, OS88_STACK_DEFAULT

mc_entry:
    call OSAPI_XMEM_CAPS            ; --- the question -----------------------
    mov [mc_kb], ax
    mov si, mc_s_caps
    call mc_say
    call mc_hex4
    mov si, mc_s_bl
    call mc_say
    mov al, bl
    call mc_hex2
    call mc_nl
    mov cl, 1
    cmp word [mc_kb], 0
    je .no
    mov ax, [mc_kb]                 ; --- a claim of exactly that ----------
    call mc_alloc
    mov si, mc_s_short
    jc .sh
    call OSAPI_XMEM_FREE
    mov cl, 2
    jc .no
    mov si, mc_s_got
    call mc_say
    call mc_nl
    jmp short .over
.sh:
    call mc_say
    call mc_hex4
    call mc_nl
.over:
    mov ax, [mc_kb]                 ; --- ...and one KB more is refused ----
    inc ax
    call mc_alloc
    mov si, mc_s_ovn
    jc .ovr
    call OSAPI_XMEM_FREE
    mov si, mc_s_ovy
.ovr:
    call mc_say
    call mc_nl
    mov si, mc_s_done
    call mc_say
    call mc_nl
    jmp short .done
.no:
    mov si, mc_s_no
    call mc_say
    mov al, cl
    call mc_hex2
    call mc_nl
.done:
    stc                             ; nothing to keep open
    ret

; mc_alloc - AX = KB -> OSAPI_XMEM_ALLOC of AX x 1024 bytes (DX:AX the token,
; CF; AX the reason on a refusal)
mc_alloc:
    mov dx, ax
    mov cl, 10
    shl ax, cl
    mov cl, 6
    shr dx, cl
    call OSAPI_XMEM_ALLOC
    ret

mc_say:
    push ax
.l:
    lodsb
    or al, al
    jz .o
    out 0xE9, al
    jmp short .l
.o:
    pop ax
    ret
mc_nl:
    push ax
    mov al, 10
    out 0xE9, al
    pop ax
    ret
mc_hex4:
    push ax
    mov al, ah
    call mc_hex2
    pop ax
mc_hex2:
    push ax
    push cx
    mov ah, al
    mov cl, 4
    shr al, cl
    call .d
    mov al, ah
    and al, 15
    call .d
    pop cx
    pop ax
    ret
.d:
    add al, '0'
    cmp al, '9'
    jbe .p
    add al, 7
.p:
    out 0xE9, al
    ret

mc_s_caps:  db 'MEGACAPS: caps ', 0
mc_s_bl:    db ' bl ', 0
mc_s_got:   db 'MEGACAPS: got', 0
mc_s_short: db 'MEGACAPS: short ', 0
mc_s_ovn:   db 'MEGACAPS: over refused', 0
mc_s_ovy:   db 'MEGACAPS: over GOT', 0
mc_s_done:  db 'MEGACAPS: done', 0
mc_s_no:    db 'MEGACAPS: no ', 0

mc_kb       dw 0

OS88_IMAGE_END
