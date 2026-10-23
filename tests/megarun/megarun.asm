; =============================================================================
; os8088 - tests/megarun/megarun.asm
;
; MEGARUN, a TEST instrument (nothing under tests/ ships): start every package
; in the folder it was launched from, one every few seconds, and say on the
; debug port which one is next - so a fault dump that follows names the
; package that caused it. docs/plans/KERN-MEGA-PLAN.md 10.1 item 16:
; tools/os88bochs.py builds a hard disk with a folder of packages and this,
; boots kern_mega under Bochs (the emulator that enforces a 286's segment
; checks, which QEMU does not), opens the folder and double-clicks this. It
; runs on any kernel and any machine; port E9 is a no-op where nothing
; listens, so on real hardware the screen is the record.
;
; One package per WAKE, and the wake is posted by a TIMER: OSAPI_PKG_START
; must be called WITHOUT the gfx lock and a timer callback holds it, while a
; wake handler is the one callback that does not (SPEC.md 74.1).
;
; E9 lines, one each:   MEGARUN: <NAME>        about to start it
;                       MEGARUN: ok | no NN    what OSAPI_PKG_START answered
;                       MEGARUN: done          the folder is exhausted
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'MEGARUN', mr_entry

MR_GAP      equ 91              ; ticks between starts: 5 seconds

mr_entry:
    push si
    push di
    mov si, mr_tpl
    call OSAPI_WM_CREATE         ; BX = window ptr
    jc .out
    OS88_REGION_MOVABLE
    mov [mr_win], bx
    mov ax, mr_tick
    call OSAPI_WM_ONTIMER
    mov ax, mr_wake
    call OSAPI_WM_ONWAKE
    mov ax, MR_GAP
    call OSAPI_WM_TIMER
    clc
.out:
    pop di
    pop si
    ret

mr_paint:                       ; W_PAINT arrives white-filled; nothing more
    ret

mr_tick:                        ; the timer, under the gfx lock: post a wake
    push bx
    mov bx, [mr_win]
    call OSAPI_WM_WAKE
    pop bx
    ret

; -----------------------------------------------------------------------------
; mr_wake - the next package in the folder, or "done"
; -----------------------------------------------------------------------------
mr_wake:
    cmp byte [mr_fin], 0
    jne .ret
.next:
    push ds
    pop es
    mov di, mr_fbuf
    mov cx, [mr_ord]
    call OSAPI_FILE_FIND
    jc .done
    mov [mr_ord], cx
    cmp word [mr_fbuf+14], OSAPI_FT_DIR
    jae .next                   ; a folder or '..'
    mov si, mr_fbuf             ; *.O88?
.end:
    lodsb
    or al, al
    jnz .end
    cmp word [si-5], '.O'
    jne .next
    cmp word [si-3], '88'
    jne .next
    cmp byte [mr_fbuf], '0'     ; ...and not us: the harness names this
    je .next                    ; 0RUN.O88, so it lists first (os88bochs.py)
.run:
    mov si, mr_s_pfx
    call mr_say
    mov si, mr_fbuf
    call mr_say
    call mr_nl
    mov si, mr_fbuf
    xor ax, ax
    mov es, ax                  ; ES = 0: no document (both arms)
    xor cx, cx                  ; DX:CX = 0: read the file
    xor dx, dx
    call OSAPI_PKG_START
    mov si, mr_s_ok
    jnc .said
    push ax
    mov si, mr_s_no
    call mr_say
    pop ax
    call mr_hex
    jmp short .rearm
.said:
    call mr_say
.rearm:
    call mr_nl
    mov bx, [mr_win]
    mov ax, MR_GAP
    call OSAPI_WM_TIMER
.ret:
    ret
.done:
    mov byte [mr_fin], 1
    mov si, mr_s_done
    call mr_say
    call mr_nl
    ret

mr_say:                         ; SI = a NUL string, to port E9
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

mr_nl:
    push ax
    mov al, 10
    out 0xE9, al
    pop ax
    ret

mr_hex:                         ; AL as two hex digits
    push ax
    push cx
    mov ah, al
    mov cl, 4
    shr al, cl
    call .dig
    mov al, ah
    and al, 15
    call .dig
    pop cx
    pop ax
    ret
.dig:
    add al, '0'
    cmp al, '9'
    jbe .put
    add al, 7
.put:
    out 0xE9, al
    ret

mr_tpl:
    dw 8, 380, 200, 40
    dw mr_ttl, mr_paint, 0, 0
mr_ttl:     db 'MegaRun', 0
mr_s_pfx:   db 'MEGARUN: ', 0
mr_s_ok:    db 'MEGARUN: ok', 0
mr_s_no:    db 'MEGARUN: no ', 0
mr_s_done:  db 'MEGARUN: done', 0

    OS88_BSS 2 + 2 + 1 + OSAPI_FIND_SZ
    OS88_IMAGE_END

mr_win      equ os88_image_end + 0
mr_ord      equ os88_image_end + 2
mr_fin      equ os88_image_end + 4
mr_fbuf     equ os88_image_end + 5
