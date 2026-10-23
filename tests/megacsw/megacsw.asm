; =============================================================================
; os8088 - tests/megacsw/megacsw.asm
;
; MEGACSW / MEGACSL, a TEST instrument (nothing under tests/ ships): a
; package that stores its own DS and its own CS THROUGH CS - `mov [cs:x], ds`
; and `mov [cs:x], cs`, 8C /3 and 8C /1 - and reads them back, for kern_mega's
; CS-write trampoline (mega_gpw, docs/plans/KERN-MEGA-PLAN.md 10.3 items 60
; and 63, 14.39). The trampoline runs the instruction with DS = the image's
; data segment and CS = KERNEL_SEG, so before 14.39 both stored the wrong
; segment. One source, two packages: MEGACSW says OS88_HIGH (-DCSW_HIGH), so
; on kern_mega its region is a code/data PAIR above 1MB; MEGACSL is ordinary,
; its CS an LDT alias of a conventional region - and MEGACSL ends with the
; GUARD (14.39, item 59): a store through the LDT alias of segment 0020h, in
; no heap claim, which kern_mega must REPORT as a fault rather than complete
; at 0020:01FE. Lines on port E9, one each:
;
;   MEGACSx: ds SSSS stored TTTT ok|BAD      DS, and what `mov [cs:x], ds` put
;   MEGACSx: cs SSSS stored TTTT ok|BAD      CS, and what `mov [cs:x], cs` put
;   MEGACSx: dsbx SSSS stored TTTT ok|BAD    the same with [cs:bx+disp16]
;   MEGACSx: dsx SSSS stored TTTT ok|BAD     DS = ES (not our own segment)
;   MEGACSx: es SSSS stored TTTT ok|BAD      ES, a control: the trampoline
;                                            never changes it
;   MEGACSx: ok | BAD
;   MEGACSL: guard next                      ...then, on kern_mega, a
;                                            `kern_mega FAULT` and nothing
;   MEGACSL: guard WROTE                     the store completed: the defect
;
; Any kernel runs it, and in real mode every store is right by construction;
; the guard is attempted only under CPU_F_PM.
; =============================================================================

%include "os88api.inc"

%ifdef CSW_HIGH
    OS88_HEADER 'MEGACSW', mc_entry, 0, OS88_STACK_192 | OS88_HIGH
%define MC_TAG 'W'
%else
    OS88_HEADER 'MEGACSL', mc_entry
%define MC_TAG 'L'
%endif

mc_entry:
    push si
    push di
    push bx
    call OSAPI_CPU_INFO
    mov [mc_cpuf], ah
    mov byte [mc_bad], 0
    mov word [mc_v], 0              ; --- mov [cs:x], ds -------------------
    mov [cs:mc_v], ds
    mov si, mc_s_ds
    mov ax, ds
    call mc_line
    mov word [mc_v], 0              ; --- mov [cs:x], cs -------------------
    mov [cs:mc_v], cs
    mov si, mc_s_cs
    mov ax, cs
    call mc_line
    mov word [mc_v2], 0             ; --- [cs:bx+disp16] -------------------
    mov bx, mc_v2 - mc_v
    mov [cs:bx+mc_v], ds
    mov ax, [mc_v2]
    mov [mc_v], ax
    mov si, mc_s_dsbx
    mov ax, ds
    call mc_line
    mov word [mc_v], 0              ; --- DS NOT our own: ES's, the kernel's
    push ds                         ; data at entry. The case the trampoline
    push es                         ; got wrong for a package of either kind:
    pop ds                          ; it ran the store with DS = OUR segment
    mov [cs:mc_v], ds
    mov ax, ds
    pop ds
    mov si, mc_s_dsx
    call mc_line
    mov word [mc_v], 0              ; --- ES, the control ------------------
    mov [cs:mc_v], es
    mov si, mc_s_es
    mov ax, es
    call mc_line
    mov si, mc_s_ok
    cmp byte [mc_bad], 0
    je .v
    mov si, mc_s_bad
.v:
    call mc_hdr
    call mc_say
    call mc_nl
%ifndef CSW_HIGH
    test byte [mc_cpuf], CPU_F_PM   ; --- THE GUARD, protected mode only ---
    jz .out
    mov si, mc_s_gnext
    call mc_hdr
    call mc_say
    call mc_nl
    mov ax, (0x0020 >> 2) | 4       ; the LDT alias of 0020h: no claim there
    mov es, ax
    mov word [es:0x01FE], 0x1234    ; ...a #GP(0) kern_mega must REPORT
    push ds
    pop es
    mov si, mc_s_gwrote
    call mc_hdr
    call mc_say
    call mc_nl
%endif
.out:
    pop bx
    pop di
    pop si
    stc                             ; no window: the instance ends here
    ret

; AX = the value the register had, [mc_v] = what the store put; SI = the
; label. One line, and [mc_bad] set on a mismatch.
mc_line:
    push ax
    call mc_hdr
    call mc_say
    call mc_hex4
    mov si, mc_s_st
    call mc_say
    mov ax, [mc_v]
    call mc_hex4
    pop ax
    mov si, mc_s_okt
    cmp ax, [mc_v]
    je .o
    mov si, mc_s_badt
    mov byte [mc_bad], 1
.o:
    call mc_say
    call mc_nl
    ret

mc_hdr:
    push si
    mov si, mc_s_hdr
    call mc_say
    pop si
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
    jbe .e
    add al, 7
.e:
    out 0xE9, al
    ret

mc_s_hdr    db 'MEGACS', MC_TAG, ': ', 0
mc_s_ds     db 'ds ', 0
mc_s_cs     db 'cs ', 0
mc_s_dsbx   db 'dsbx ', 0
mc_s_dsx    db 'dsx ', 0
mc_s_es     db 'es ', 0
mc_s_st     db ' stored ', 0
mc_s_okt    db ' ok', 0
mc_s_badt   db ' BAD', 0
mc_s_ok     db 'ok', 0
mc_s_bad    db 'BAD', 0
mc_s_gnext  db 'guard next', 0
mc_s_gwrote db 'guard WROTE', 0
mc_v        dw 0
mc_v2       dw 0
mc_cpuf     db 0
mc_bad      db 0

    OS88_BSS 0
    OS88_IMAGE_END
