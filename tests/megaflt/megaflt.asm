; =============================================================================
; os8088 - tests/megaflt/megaflt.asm
;
; MEGAFLT, a TEST instrument (nothing under tests/ ships): a package that
; FAULTS on purpose, so kern_mega's forced close (docs/plans/KERN-MEGA-PLAN.md
; 14.42) can be watched closing it and nothing else. One source, a build per
; case (-DFLT_MODE=n), each its own name so the toast and the E9 line say
; which:
;
;   FLTA  1  #UD in a window TIMER callback, on the UI task, gfx lock held by
;            the kernel's caller (it must STAY held for that caller)
;   FLTB  2  a WORKER takes OSAPI_GFX_LOCK and reads past its segment (#GP):
;            the worker is killed and the lock it held given back. OS88_HIGH:
;            its code is a pair's half above 1MB
;   FLTC  3  inside an OSAPI_FSX_RUN bracket, a foreign mode set: a divide by
;            zero (#DE) - the bracket's own restore must still run
;   FLTD  4  with a 32KB claim held, the timer armed and a speaker TONE on:
;            #GP in the timer callback. OS88_HIGH | OS88_HIGHMEM
;   FLTE  5  #GP in the ENTRY PROC: the launch is refused, not the machine
;   FLTF  6  a WAKE handler (no gfx lock) takes OSAPI_GFX_LOCK, then #UD: the
;            unwind must give the lock back
;   FLTZ  0  the control: it faults never, says it is alive, and echoes a key
;            and a click, which is the harness's proof that the machine is
;
; E9 lines, one each:   FLTx: avail AAAA BBBB caps CCCC   at entry, before
;                                any claim: MEM_AVAIL's AX and BX, XMEM_CAPS
;                       FLTx: tick | worker | fsx | wake  about to fault
;                       FLTZ: alive | key KK | click
; Every case's avail line must read as the one before it (each was closed
; and its region, its claims and its pool block went back).
; =============================================================================

%include "os88api.inc"

%ifndef FLT_MODE
 %define FLT_MODE 0
%endif
%if FLT_MODE == 0
 %define FLT_NAME 'FLTZ'
 %define FLT_CH 'Z'
%elif FLT_MODE == 1
 %define FLT_NAME 'FLTA'
 %define FLT_CH 'A'
%elif FLT_MODE == 2
 %define FLT_NAME 'FLTB'
 %define FLT_CH 'B'
%elif FLT_MODE == 3
 %define FLT_NAME 'FLTC'
 %define FLT_CH 'C'
%elif FLT_MODE == 4
 %define FLT_NAME 'FLTD'
 %define FLT_CH 'D'
%elif FLT_MODE == 5
 %define FLT_NAME 'FLTE'
 %define FLT_CH 'E'
%else
 %define FLT_NAME 'FLTF'
 %define FLT_CH 'F'
%endif

%if FLT_MODE == 2
    OS88_HEADER FLT_NAME, fl_entry, 0, OS88_STACK_192 | OS88_HIGH
%elif FLT_MODE == 4
    OS88_HEADER FLT_NAME, fl_entry, 0, OS88_STACK_192 | OS88_HIGH | OS88_HIGHMEM
%else
    OS88_HEADER FLT_NAME, fl_entry
%endif

fl_entry:
    push si
    push di
    mov si, fl_s_av                 ; --- what is free, before we take any
    call fl_say
    call OSAPI_MEM_AVAIL
    call fl_hex4
    mov al, ' '
    out 0xE9, al
    mov ax, bx
    call fl_hex4
    mov si, fl_s_caps
    call fl_say0
    call OSAPI_XMEM_CAPS
    call fl_hex4
    call fl_nl
%if FLT_MODE == 5
    mov ax, [0xFFFF]                ; past the segment: #GP in the entry proc
%endif
%if FLT_MODE == 4
    mov ax, 32                      ; a claim, held
    call OSAPI_MEM_CLAIM
    jc .out
%endif
    mov si, fl_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [fl_win], bx
    mov ax, fl_tick
    call OSAPI_WM_ONTIMER
%if FLT_MODE == 6
    mov ax, fl_wake
    call OSAPI_WM_ONWAKE
%endif
    mov ax, 27                      ; a second and a half
    call OSAPI_WM_TIMER
%if FLT_MODE == 4
    mov ax, 440                     ; ...and the speaker on, until off
    xor cx, cx
    mov dl, 0x40
    call OSAPI_SND_TONE
%endif
    clc
.out:
    pop di
    pop si
    ret

fl_paint:
    ret

fl_tick:                            ; the timer, gfx lock held
%if FLT_MODE == 0
    cmp byte [fl_said], 0
    jne .r
    mov byte [fl_said], 1
    mov si, fl_s_alive
    call fl_say
    call fl_nl
.r:
    ret
%else
    mov si, fl_s_tick
    call fl_say
    call fl_nl
 %if FLT_MODE == 1
    db 0x0F, 0x0B                   ; #UD
 %elif FLT_MODE == 2
    mov ax, fl_worker
    mov bx, [fl_win]
    call OSAPI_TASK_SPAWN
    ret
 %elif FLT_MODE == 3
    mov ax, fl_fsx
    mov bx, [fl_win]
    xor cx, cx
    call OSAPI_FSX_RUN
    ret
 %elif FLT_MODE == 4
    mov ax, [0xFFFF]                ; #GP
 %elif FLT_MODE == 6
    mov bx, [fl_win]
    call OSAPI_WM_WAKE
 %endif
    ret
%endif

%if FLT_MODE == 2
fl_worker:
    mov si, fl_s_work
    call fl_say
    call fl_nl
    mov cx, 3
.l:
    mov bx, [fl_win]
    call OSAPI_TASK_ALIVE
    mov ax, 4
    call OSAPI_TASK_SLEEP
    loop .l
    call OSAPI_GFX_LOCK             ; held when it dies: the kill gives it back
    mov ax, [0xFFFF]                ; #GP
    jmp $
%endif

%if FLT_MODE == 3
fl_fsx:                             ; the bracket's proc: a foreign mode, then
    push ds                         ; a divide by zero
    pop es
    mov di, fl_fsi
    mov al, FSXM_CGA320
    call OSAPI_FSX_MODE
    mov si, fl_s_fsx
    call fl_say
    call fl_nl
    xor cx, cx
    xor dx, dx
    div cx                          ; #DE
    ret
%endif

%if FLT_MODE == 6
fl_wake:                            ; a wake: no gfx lock until we take it
    call OSAPI_GFX_LOCK
    mov si, fl_s_wake
    call fl_say
    call fl_nl
    db 0x0F, 0x0B                   ; #UD with the lock held
    ret
%endif

%if FLT_MODE == 0
fl_key:                             ; AL = ascii, AH = scan
    push ax
    mov si, fl_s_key
    call fl_say
    pop ax
    call fl_hex2
    call fl_nl
    mov si, fl_t_key                ; ...and on the glass, for a machine with
    jmp short fl_toast              ; no debug port (86Box)
fl_click:
    mov si, fl_s_click
    call fl_say
    call fl_nl
    mov si, fl_t_click
fl_toast:
    push ds
    pop es
    xor cx, cx
    call OSAPI_TOAST
    ret
%endif

fl_say:                             ; "FLTx: " then SI
    push si
    mov si, fl_s_pfx
    call fl_say0
    pop si
fl_say0:
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
fl_nl:
    push ax
    mov al, 10
    out 0xE9, al
    pop ax
    ret
fl_hex4:
    push ax
    mov al, ah
    call fl_hex2
    pop ax
fl_hex2:
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

fl_tpl:
    dw 40, 120, 200, 60
%if FLT_MODE == 0
    dw fl_ttl, fl_paint, fl_key, fl_click
%else
    dw fl_ttl, fl_paint, 0, 0
%endif
fl_ttl:     db FLT_NAME, 0
fl_s_pfx:   db 'FLT', FLT_CH, ': ', 0
fl_s_av:    db 'avail ', 0
fl_s_caps:  db ' caps ', 0
fl_s_tick:  db 'tick', 0
fl_s_work:  db 'worker', 0
fl_s_fsx:   db 'fsx', 0
fl_s_wake:  db 'wake', 0
fl_s_alive: db 'alive', 0
fl_s_key:   db 'key ', 0
fl_s_click: db 'click', 0
%if FLT_MODE == 0
fl_t_key:   db 'FLTZ got a key', 0
fl_t_click: db 'FLTZ got a click', 0
%endif

    OS88_BSS 2 + 1 + FSI_SIZE
    OS88_IMAGE_END

fl_win      equ os88_image_end + 0
fl_said     equ os88_image_end + 2
fl_fsi      equ os88_image_end + 3
