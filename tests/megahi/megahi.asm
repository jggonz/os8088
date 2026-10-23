; =============================================================================
; os8088 - tests/megahi/megahi.asm
;
; MEGAHI, a TEST instrument (nothing under tests/ ships): a package that says
; OS88_HIGH (docs/plans/KERN-MEGA-PLAN.md 13.3 step 4), so under CPU_F_PM
; kern_mega loads its region ABOVE 1MB, and every door a package uses is then
; walked with DS a selector whose base is past the megabyte. Every result on
; port E9, one line each, for tools/os88bochs.py:
;
;   MEGAHI: ds SSSS caps KKKK    DS at entry (a pair's data half: bit 2 set
;                                under protected mode) and XMEM_CAPS' free KB
;   MEGAHI: claim SSSS ok        OSAPI_MEM_CLAIM from the ENTRY PROC, written
;                                and read back, and freed. OS88_HIGHMEM: under
;                                protected mode SSSS is a SELECTOR (bit 2 set)
;                                over a pool block; a second claim of 8KB is
;                                left HELD, for the teardown to give back
;   MEGAHI: regrow SSSS TTTT ok  OSAPI_MEM_REGROW of that claim to 8KB keeps
;                                SSSS and the bytes (in the pool), then to
;                                80KB MOVES it down to TTTT, a conventional
;                                segment, bytes and all
;   MEGAHI: huge SSSS step NNNN ok  OSAPI_MEM_HUGE of 200KB: four pieces
;                                walked by the step, each written and read
;                                back, and freed - step 0008 above 1MB
;   MEGAHI: claim file ok        the held claim, filled by OSAPI_FILE_READ
;   MEGAHI: file ok              its own .o88 read into its bss: 'O8'
;   MEGAHI: tick                 a timer callback ran (wm_pkgcall's CS)
;   MEGAHI: worker               a worker ran (task_spawn's frame CS)
;   MEGAHI: ok | no NN           the verdict, or the step that refused
;
; Run it twice: the second line's caps must equal the first's, which is the
; pair and its pool block given back when the window closed (mem_free_rec_x).
; On a kernel without protected mode it runs low and says so with its DS.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'MEGAHI', mh_entry, 0, OS88_STACK_192 | OS88_HIGH | OS88_HIGHMEM

mh_entry:
    push si
    push di
    push bp
    mov si, mh_s_ds                 ; --- where we run ---------------------
    call mh_say
    mov ax, ds
    call mh_hex4
    mov si, mh_s_caps
    call mh_say
    call OSAPI_XMEM_CAPS
    call mh_hex4
    call mh_nl
    mov ax, 1                       ; --- a claim from the entry proc ------
    call OSAPI_MEM_CLAIM
    mov cl, 1
    jc .no
    mov es, dx
    mov word [es:100], 0x5A5A
    cmp word [es:100], 0x5A5A
    mov cl, 2
    jne .no
    mov word [es:1022], 0xA5A5      ; ...its last word, inside the window
    mov si, mh_s_claim
    call mh_say
    mov ax, dx
    call mh_hex4
    mov si, mh_s_okt
    call mh_say
    call mh_nl
    mov bp, dx                      ; --- REGROW: in the pool, DX kept ------
    mov ax, 8
    call OSAPI_MEM_REGROW
    mov cl, 0x10
    jc .no
    mov cl, 0x11
    cmp dx, bp
    jne .no                         ; (conventionally it could move; in the
    mov es, dx                      ; pool it must not)
    mov cl, 0x12
    cmp word [es:100], 0x5A5A
    jne .no
    cmp word [es:1022], 0xA5A5
    jne .no
    mov word [es:8190], 0x7E7E      ; the new last word
    mov ax, 80                      ; --- ...past 64KB: MOVED DOWN ----------
    call OSAPI_MEM_REGROW
    mov cl, 0x13
    jc .no
    mov es, dx
    mov cl, 0x14
    cmp word [es:100], 0x5A5A
    jne .no
    cmp word [es:8190], 0x7E7E
    jne .no
    mov si, mh_s_regrow
    call mh_say
    mov ax, bp
    call mh_hex4
    mov al, ' '
    out 0xE9, al
    mov ax, dx
    call mh_hex4
    mov si, mh_s_okt
    call mh_say
    call mh_nl
    call OSAPI_MEM_FREE
    mov cl, 3
    jc .no
    mov ax, 200                     ; --- OSAPI_MEM_HUGE: 200KB, four pieces
    call OSAPI_MEM_HUGE             ; walked by the step it answers (KERN-
    mov [mh_hstep], cx              ; MEGA-PLAN 14.1 answers 6, 7)
    mov cl, 0x30
    jc .no
    mov cx, [mh_hstep]
    mov bp, dx                      ; BP = the first, CX = the step
    xor ax, ax
.hw:
    mov es, dx                      ; piece i holds i at 0 and ~i at its last
    mov [es:0], ax                  ; word (64KB - 2, or 8KB - 2 for the
    not ax                          ; fourth, the claim's end)
    mov di, 0xFFFE
    cmp ax, ~3 & 0xFFFF
    jne .hl
    mov di, 8190
.hl:
    mov [es:di], ax
    not ax
    inc ax
    add dx, cx
    cmp ax, 4
    jb .hw
    mov dx, bp                      ; ...and every one read back, so no two
    xor ax, ax                      ; pieces are the same memory
.hr:
    mov es, dx
    mov cl, 0x31
    cmp [es:0], ax
    jne .hbad
    add dx, [mh_hstep]
    inc ax
    cmp ax, 4
    jb .hr
    jmp short .hok
.hbad:
    jmp .no
.hok:
    mov si, mh_s_huge
    call mh_say
    mov ax, bp
    call mh_hex4
    mov si, mh_s_step
    call mh_say
    mov ax, [mh_hstep]
    call mh_hex4
    mov si, mh_s_okt
    call mh_say
    call mh_nl
    mov dx, bp
    call OSAPI_MEM_FREE
    mov cl, 0x32
    jc .no
    mov ax, 200                     ; ...and the FREE gave the whole run back:
    call OSAPI_MEM_HUGE             ; the same claim again lands on the same
    mov cl, 0x33                    ; first selector, which it cannot if the
    jc .no                          ; run's later entries were left taken
    cmp dx, bp
    jne .no
    call OSAPI_MEM_FREE
    mov cl, 0x34
    jc .no
    mov ax, 8                       ; ...and one LEFT HELD: the teardown's
    call OSAPI_MEM_CLAIM
    mov cl, 9
    jc .no
    push dx                         ; --- a file into that claim -----------
    mov es, dx
    xor bx, bx
    xor dx, dx
    mov cx, 8192
    mov si, mh_s_self
    call OSAPI_FILE_READ
    pop dx
    mov cl, 0x15
    jc .no
    mov cl, 0x16
    cmp word [es:0], 0x384F
    jne .no
    mov si, mh_s_cfile
    call mh_say
    call mh_nl
    push ds                         ; --- a file into our own bss ----------
    pop es
    mov bx, mh_buf
    xor dx, dx
    mov cx, MH_BUFSZ
    mov si, mh_s_self
    call OSAPI_FILE_READ
    mov cl, 4
    jc .no
    mov cl, 5
    cmp word [mh_buf], 0x384F       ; 'O8'
    jne .no
    mov si, mh_s_file
    call mh_say
    call mh_nl
    mov si, mh_tpl                  ; --- a window, a timer -----------------
    call OSAPI_WM_CREATE
    mov cl, 6
    jc .no
    OS88_REGION_MOVABLE
    mov [mh_win], bx
    mov ax, mh_tick
    call OSAPI_WM_ONTIMER
    mov ax, 9
    call OSAPI_WM_TIMER
    clc
    jmp short .out
.no:
    mov si, mh_s_no
    call mh_say
    mov al, cl
    call mh_hex2
    call mh_nl
    stc
.out:
    pop bp
    pop di
    pop si
    ret

mh_paint:
    ret

mh_tick:                            ; the timer, gfx lock held
    push ax
    push bx
    mov si, mh_s_tick
    call mh_say
    call mh_nl
    cmp byte [mh_spawned], 0
    jne .wait
    mov byte [mh_spawned], 1
    mov ax, mh_worker               ; --- a worker -------------------------
    mov bx, [mh_win]
    call OSAPI_TASK_SPAWN
    mov cl, 7
    jc .no
    mov bx, [mh_win]
    mov ax, 18
    call OSAPI_WM_TIMER
    jmp short .out
.wait:
    cmp byte [mh_wran], 0
    mov cl, 8
    je .no
    mov si, mh_s_ok
    call mh_say
    call mh_nl
    mov bx, [mh_win]                ; done: the window goes, and with it the
    call OSAPI_WM_DESTROY           ; instance, the pair and its block
    jmp short .out
.no:
    mov si, mh_s_no
    call mh_say
    mov al, cl
    call mh_hex2
    call mh_nl
.out:
    pop bx
    pop ax
    ret

mh_worker:                          ; a worker: DS = ours, its own stack. It
    mov si, mh_s_work               ; NEVER RETURNS (there is nothing under
    call mh_say                     ; its frame): it lives in TASK_ALIVE until
    call mh_nl                      ; the window goes, and ends inside it
    mov byte [mh_wran], 1
.live:
    mov bx, [mh_win]
    call OSAPI_TASK_ALIVE
    mov ax, 2
    call OSAPI_TASK_SLEEP
    jmp short .live

mh_say:
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
mh_nl:
    push ax
    mov al, 10
    out 0xE9, al
    pop ax
    ret
mh_hex4:
    push ax
    mov al, ah
    call mh_hex2
    pop ax
mh_hex2:
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

mh_tpl:
    dw 8, 300, 200, 40
    dw mh_ttl, mh_paint, 0, 0
mh_ttl:     db 'MegaHi', 0
mh_s_ds:    db 'MEGAHI: ds ', 0
mh_s_caps:  db ' caps ', 0
mh_s_claim: db 'MEGAHI: claim ', 0
mh_s_okt:   db ' ok', 0
mh_s_file:  db 'MEGAHI: file ok', 0
mh_s_regrow: db 'MEGAHI: regrow ', 0
mh_s_huge:  db 'MEGAHI: huge ', 0
mh_s_step:  db ' step ', 0
mh_hstep:   dw 0
mh_s_cfile: db 'MEGAHI: claim file ok', 0
mh_s_tick:  db 'MEGAHI: tick', 0
mh_s_work:  db 'MEGAHI: worker', 0
mh_s_ok:    db 'MEGAHI: ok', 0
mh_s_no:    db 'MEGAHI: no ', 0
mh_s_self:  db 'MEGAHI.O88', 0

MH_BUFSZ    equ 4096

    OS88_BSS 4 + MH_BUFSZ
    OS88_IMAGE_END

mh_win      equ os88_image_end + 0
mh_spawned  equ os88_image_end + 2
mh_wran     equ os88_image_end + 3
mh_buf      equ os88_image_end + 4
