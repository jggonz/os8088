; =============================================================================
; os8088 - tests/megai13/megai13.asm
;
; MEGAI13, a TEST instrument (nothing under tests/ ships): REFLECTED BIOS
; calls back to back, for the pointer to move through (docs/plans/KERN-MEGA-
; PLAN.md 14.1 answer 18). On kern_mega the native floppy path takes only the
; file layer's transfers (mega_fd13's [mega_fdx]), so a package's OWN int 13h
; is reflected: this reads drive A's track 0, 18 sectors, MI_N times into a
; conventional claim, from its entry proc, and says so on port E9:
;
;   MEGAI13: start             the first call is about to be made
;   MEGAI13: done | no NN      the last one returned, or the one that refused
;
; A harness moves the mouse between the two lines and reads where the
; pointer went. Any kernel runs it; only kern_mega reflects.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'MEGAI13', mi_entry

MI_N        equ 40

mi_entry:
    push si
    mov ax, 9                       ; 18 sectors
    call OSAPI_MEM_CLAIM
    mov cl, 1
    jc .no
    mov si, mi_s_start
    call mi_say
    mov es, dx
    mov cx, MI_N
.l:
    push cx
    xor bx, bx
    mov ax, 0x0212
    mov cx, 0x0001
    xor dx, dx
    int 0x13
    pop cx
    jc .rd
    loop .l
    mov si, mi_s_done
    call mi_say
    jmp short .out
.rd:
    mov cl, 2
.no:
    mov si, mi_s_no
    call mi_say
.out:
    pop si
    stc                             ; no window: the instance ends here
    ret

mi_say:
    push ax
.c:
    mov al, [si]
    or al, al
    jz .e
    out 0xE9, al
    inc si
    jmp short .c
.e:
    mov al, 10
    out 0xE9, al
    pop ax
    ret

mi_s_start: db 'MEGAI13: start', 0
mi_s_done:  db 'MEGAI13: done', 0
mi_s_no:    db 'MEGAI13: no', 0

    OS88_BSS 0
    OS88_IMAGE_END
