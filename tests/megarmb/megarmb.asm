; =============================================================================
; os8088 - tests/megarmb/megarmb.asm
;
; MEGARMB, a TEST instrument (nothing under tests/ ships): kern_mega's
; REAL-MODE BRACKET (SPEC.md 53.2.3, docs/plans/MEGA-VIDEO-REALMODE-PLAN.md
; W1) from a package, with every result on port E9 for tests/megarmb.py
; (Bochs) and in its own bss for tests/megarmb286.py (86Box, which has no E9
; but reads guest memory). Assembled twice: MEGARMB says OS88_HIGH, so on
; kern_mega in protected mode its region is above 1MB and the bracket runs a
; COPY of it below; MEGARMBL (-DMB_LOW) does not, and runs where it is.
;
; Its window's timer starts it once, two seconds after launch (a timer
; callback is a window callback under the gfx lock, which OSAPI_FSX_RUN
; wants). In order:
;
;   R  FSXF_REAL with FSXF_KEEPWORKER: on kern_mega in protected mode real
;      mode cannot be had (one task runs there), so the bracket FALLS BACK
;      (W2): it runs in protected mode, SI = FSXR_KEEP, and the entry sees
;      CPU_F_PM - and on every kernel in real mode the bit is ignored
;   A  FSXF_REAL + FSXF_RATE at MB_DIV (291 Hz): the entry proc banks its CS
;      and OSAPI_CPU_INFO's AH, reads [ticks] and 40:6C under one cli, sets
;      ready and halts until the hook has been handed MB_PER periods, reads
;      both clocks again under one cli, and writes MB_MARK into its bss. The
;      hook counts calls and periods and the most handed to one call, and
;      every 16th call sti's and spins two periods long, so a nested IRQ0
;      must be SKIPPED and its period handed on
;   B  FSXF_REAL alone: OSAPI_FSX_MODE to mode 13h INSIDE the real-mode
;      bracket (a cell that reads the video words step 3 made segments),
;      FSI_SEG banked, then MB_BT ticks of halting
;   and around all of it, [ticks] - 40:6C under one cli before R and after B
;
; E9 lines:  <NAME>: start
;            <NAME>: R cf C why WW feat FF
;            <NAME>: A cf C why WW cs CCCC feat FF calls NNNN periods PPPPPPPP max MM
;                    ticks TTTT bios BBBB mark MMMM
;            <NAME>: B cf C why WW seg SSSS ticks TTTT bios BBBB
;            <NAME>: off OOOO OOOO      ([ticks] - 40:6C, before and after)
;            <NAME>: done
; =============================================================================

%include "os88api.inc"

%ifdef MB_LOW
    OS88_HEADER 'MEGARMBL', mb_entry, 0, OS88_STACK_192
%else
    OS88_HEADER 'MEGARMB', mb_entry, 0, OS88_STACK_192 | OS88_HIGH
%endif

MB_DIV      equ 4096                ; 1,193,182 / 4096 = 291.3 Hz
MB_PER      equ 1200                ; ~4.1 s of periods: room for a harness
                                    ; to stop the machine INSIDE and look
MB_BT       equ 36                  ; leg B's two seconds of ticks
MB_MARK     equ 0xA55A
MB_WAIT     equ 36                  ; the timer: two seconds after launch

mb_entry:
    push si
    mov si, mb_s_start
    call mb_say
    call mb_nl
    mov si, mb_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [mb_win], bx
    mov ax, mb_tick
    call OSAPI_WM_ONTIMER
    mov ax, MB_WAIT
    call OSAPI_WM_TIMER
    clc
.out:
    pop si
    ret

mb_paint:
    ret

; the timer: under the gfx lock, a window callback - the bracket's context
mb_tick:
    cmp byte [mb_ran], 0
    je .go
    ret
.go:
    mov byte [mb_ran], 1
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    call mb_clocks                  ; AX = [ticks] - 40:6C, before
    mov [mb_off0], ax
    ; --- R: KEEPWORKER with REAL ------------------------------------------
    mov bx, [mb_win]
    mov ax, mb_main_r
    mov cx, FSXF_REAL + FSXF_KEEPWORKER
    xor si, si                      ; FSXR_REAL in: SI is the answer out
    xor bp, bp                      ; no FSXL_ list (W3)
    call OSAPI_FSX_RUN
    sbb ax, ax
    mov [mb_rcf], al
    mov ax, si
    mov [mb_rwhy], al
    mov si, mb_s_r
    call mb_say
    mov al, [mb_rcf]
    and al, 1
    call mb_dig
    mov si, mb_s_why
    call mb_puts
    mov al, [mb_rwhy]
    call mb_hex2
    mov si, mb_s_feat
    call mb_puts
    mov al, [mb_rfeat]
    call mb_hex2
    call mb_nl
    ; --- A: REAL + RATE ----------------------------------------------------
    mov bx, [mb_win]
    mov ax, mb_main_a
    mov cx, FSXF_REAL + FSXF_RATE
    mov dx, MB_DIV
    mov di, mb_hook
    xor si, si
    xor bp, bp                      ; no FSXL_ list (W3)
    call OSAPI_FSX_RUN
    sbb ax, ax
    mov [mb_acf], al
    mov ax, si
    mov [mb_awhy], al
    mov si, mb_s_a
    call mb_say
    mov al, [mb_acf]
    and al, 1
    call mb_dig
    mov si, mb_s_why
    call mb_puts
    mov al, [mb_awhy]
    call mb_hex2
    mov si, mb_s_cs
    call mb_puts
    mov ax, [mb_cs]
    call mb_hex4
    mov si, mb_s_feat
    call mb_puts
    mov al, [mb_feat]
    call mb_hex2
    mov si, mb_s_calls
    call mb_puts
    mov ax, [mb_calls]
    call mb_hex4
    mov si, mb_s_per
    call mb_puts
    mov ax, [mb_per+2]
    call mb_hex4
    mov ax, [mb_per]
    call mb_hex4
    mov si, mb_s_max
    call mb_puts
    mov ax, [mb_max]
    call mb_hex4
    mov si, mb_s_ticks
    call mb_puts
    mov ax, [mb_adt]
    call mb_hex4
    mov si, mb_s_bios
    call mb_puts
    mov ax, [mb_adb]
    call mb_hex4
    mov si, mb_s_mark
    call mb_puts
    mov ax, [mb_mark]
    call mb_hex4
    call mb_nl
    ; --- B: REAL alone, a mode set inside ----------------------------------
    mov bx, [mb_win]
    mov ax, mb_main_b
    mov cx, FSXF_REAL
    xor si, si
    xor bp, bp                      ; no FSXL_ list (W3)
    call OSAPI_FSX_RUN
    sbb ax, ax
    mov [mb_bcf], al
    mov ax, si
    mov [mb_bwhy], al
    mov si, mb_s_b
    call mb_say
    mov al, [mb_bcf]
    and al, 1
    call mb_dig
    mov si, mb_s_why
    call mb_puts
    mov al, [mb_bwhy]
    call mb_hex2
    mov si, mb_s_seg
    call mb_puts
    mov ax, [mb_bseg]
    call mb_hex4
    mov si, mb_s_ticks
    call mb_puts
    mov ax, [mb_bdt]
    call mb_hex4
    mov si, mb_s_bios
    call mb_puts
    mov ax, [mb_bdb]
    call mb_hex4
    call mb_nl
    call mb_clocks                  ; ...and after
    mov [mb_off1], ax
    mov si, mb_s_off
    call mb_say
    mov ax, [mb_off0]
    call mb_hex4
    mov al, ' '
    out 0xE9, al
    mov ax, [mb_off1]
    call mb_hex4
    call mb_nl
    mov si, mb_s_done
    call mb_say
    call mb_nl
    mov byte [mb_fin], 1
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mb_clocks - AX = [ticks] - 40:6C, both read under one cli. Clobbers DX
mb_clocks:
    push es
    pushf
    cli
    call OSAPI_GET_TICKS
    mov dx, ax
    mov ax, 0x40
    mov es, ax
    mov ax, [es:0x6C]
    popf
    pop es
    sub dx, ax
    mov ax, dx
    ret

; mb_both - DX = [ticks], AX = 40:6C, under one cli
mb_both:
    push es
    pushf
    cli
    call OSAPI_GET_TICKS
    mov dx, ax
    mov ax, 0x40
    mov es, ax
    mov ax, [es:0x6C]
    popf
    pop es
    ret

; R's entry: in PROTECTED mode on kern_mega (the fallback, W2)
mb_main_r:
    call OSAPI_CPU_INFO
    mov [mb_rfeat], ah
    ret

; A's entry - REAL MODE on kern_mega: SI = the window, DS = CS = ours
mb_main_a:
    mov [mb_cs], cs
    call OSAPI_CPU_INFO
    mov [mb_feat], ah
    call mb_both
    mov [mb_at0], dx
    mov [mb_ab0], ax
    mov byte [mb_ready], 1
.w:
    sti
    hlt
    cmp word [mb_per+2], 0
    jne .done
    cmp word [mb_per], MB_PER
    jb .w
.done:
    cli
    mov byte [mb_ready], 0
    sti
    call mb_both
    sub dx, [mb_at0]
    mov [mb_adt], dx
    sub ax, [mb_ab0]
    mov [mb_adb], ax
    mov word [mb_mark], MB_MARK     ; written in the copy we run in
    ret

; the hook: AX = the periods since it last ran, IF = 0
mb_hook:
    cmp byte [mb_ready], 0
    je .out
    inc word [mb_calls]
    add [mb_per], ax
    adc word [mb_per+2], 0
    cmp ax, [mb_max]
    jbe .m
    mov [mb_max], ax
.m:
    test word [mb_calls], 15
    jnz .out
    sti                             ; every 16th: two periods long with IF
    push cx                         ; on, so the next IRQ0 lands in here
    mov cx, [mb_spin]
    loop $
    pop cx
    cli
.out:
    ret

; B's entry: a mode set in real mode, then MB_BT ticks
mb_main_b:
    push es
    push ds
    pop es
    mov di, mb_fsi
    mov al, FSXM_VGA13
    call OSAPI_FSX_MODE
    pop es
    mov ax, 0xFFFF
    jc .nomode
    mov ax, [mb_fsi+FSI_SEG]
.nomode:
    mov [mb_bseg], ax
    call mb_both
    mov [mb_bt0], dx
    mov [mb_bb0], ax
.w:
    sti
    hlt
    call OSAPI_GET_TICKS
    sub ax, [mb_bt0]
    cmp ax, MB_BT
    jb .w
    call mb_both
    sub dx, [mb_bt0]
    mov [mb_bdt], dx
    sub ax, [mb_bb0]
    mov [mb_bdb], ax
    ret

; --- E9 ----------------------------------------------------------------------
mb_say:                             ; the name, ": ", then [SI]
    push si
    mov si, mb_name
    call mb_puts
    pop si
mb_puts:
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
mb_nl:
    push ax
    mov al, 10
    out 0xE9, al
    pop ax
    ret
mb_dig:
    add al, '0'
    out 0xE9, al
    ret
mb_hex4:
    push ax
    mov al, ah
    call mb_hex2
    pop ax
mb_hex2:
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

mb_tpl:
    dw 8, 300, 200, 40
    dw mb_ttl, mb_paint, 0, 0
%ifdef MB_LOW
mb_ttl:     db 'MegaRMB low', 0
mb_name:    db 'MEGARMBL: ', 0
%else
mb_ttl:     db 'MegaRMB', 0
mb_name:    db 'MEGARMB: ', 0
%endif
mb_s_start: db 'start', 0
mb_s_r:     db 'R cf ', 0
mb_s_a:     db 'A cf ', 0
mb_s_cs:    db ' cs ', 0
mb_s_why:   db ' why ', 0
mb_s_feat:  db ' feat ', 0
mb_s_calls: db ' calls ', 0
mb_s_per:   db ' periods ', 0
mb_s_max:   db ' max ', 0
mb_s_ticks: db ' ticks ', 0
mb_s_bios:  db ' bios ', 0
mb_s_mark:  db ' mark ', 0
mb_s_b:     db 'B cf ', 0
mb_s_seg:   db ' seg ', 0
mb_s_off:   db 'off ', 0
mb_s_done:  db 'done', 0
mb_spin:    dw 30000                ; the hook's long call (a harness may
                                    ; patch it: tests/megarmb.py reads it)

; THE RESULT BLOCK, in the bss and in this order - tests/megarmb286.py reads
; it as one span at mb_res (MB_RESN bytes), so the offsets are the ABI
    OS88_BSS MB_RESN + FSI_SIZE
    OS88_IMAGE_END

mb_res      equ os88_image_end
mb_fin      equ mb_res + 0          ; byte: 1 = every leg ran
mb_rcf      equ mb_res + 1          ; byte: R's CF (FFh refused)
mb_acf      equ mb_res + 2
mb_bcf      equ mb_res + 3
mb_cs       equ mb_res + 4          ; word: A's CS, inside
mb_feat     equ mb_res + 6          ; byte: OSAPI_CPU_INFO's AH, inside
mb_ready    equ mb_res + 7
mb_calls    equ mb_res + 8          ; word
mb_per      equ mb_res + 10         ; dword
mb_max      equ mb_res + 14
mb_adt      equ mb_res + 16         ; A: [ticks] moved
mb_adb      equ mb_res + 18         ; A: 40:6C moved
mb_mark     equ mb_res + 20
mb_bseg     equ mb_res + 22         ; B: FSI_SEG, FFFFh no mode
mb_bdt      equ mb_res + 24
mb_bdb      equ mb_res + 26
mb_off0     equ mb_res + 28
mb_off1     equ mb_res + 30
mb_ran      equ mb_res + 32
mb_win      equ mb_res + 34
mb_at0      equ mb_res + 36
mb_ab0      equ mb_res + 38
mb_bt0      equ mb_res + 40
mb_bb0      equ mb_res + 42
mb_rwhy     equ mb_res + 44         ; byte: R's SI out, FSXR_* (W2)
mb_rfeat    equ mb_res + 45         ; byte: OSAPI_CPU_INFO's AH inside R
mb_awhy     equ mb_res + 46
mb_bwhy     equ mb_res + 47
MB_RESN     equ 48
mb_fsi      equ mb_res + MB_RESN
