; =============================================================================
; os8088 - apps/win16/w16i21.inc
;
; DOS AS THE PROGRAM'S C RUNTIME CALLS IT (SPEC.md 124.3, 124.4.1). The MS C
; and Open Watcom start-up and exit issue int 21h themselves rather than
; through DOS3Call: S1's census found AH = 25h, 30h, 35h, 3Eh, 40h, 44h and
; 4Ch in CALC 3.00 and 25h, 30h, 35h and 4Ch in WinMine 2.9. This is only
; that set, and the clock: 2Ch, which CLOCK 3.00 reads the time with, and
; 2Ah, the date, which Notepad and Calendar ask for (SPEC.md 124.17.3), both
; off OSAPI_CLK_GET. Anything else answers "invalid function", which a
; runtime treats as an ordinary refusal.
;
; It runs on the PROGRAM's stack (it is the program's own `int`), with the
; program's DS, IF = 0 from the gate. Only AH=4Ch leaves it other than by
; iret: it switches to the host stack and returns from w16_resume, and the
; program's frame is simply abandoned with its claim.
; =============================================================================

W16_IRET_FL equ 6               ; FLAGS in the iret frame, above the pushed BP

w16_i21:
    ; STKBALANCE-OK: an interrupt handler on the PROGRAM's stack; every arm
    ; but AH=4Ch leaves by iret, and that one by w16_i21_exit's switch home
    cmp ah, 0x4C
    je w16_i21_exit
    push bp
    mov bp, sp
%ifdef W16_TRACE
    push bx
    push ax
    mov bx, [cs:w16_trn]
    and bx, 127
    shl bx, 1
    mov al, ah
    mov ah, 0x80
    mov [cs:w16_trc + bx], ax
    inc word [cs:w16_trn]
    pop ax
    pop bx
%endif
    and word [bp + W16_IRET_FL], 0xFFFE     ; CF = 0 unless refused below
    cmp ah, 0x30
    je .ver
    cmp ah, 0x25
    je .setv
    cmp ah, 0x35
    je .getv
    cmp ah, 0x40
    je .write
    cmp ah, 0x3E
    je .close
    cmp ah, 0x44
    je .ioctl
    cmp ah, 0x2A
    jb .bad
    cmp ah, 0x2C
    ja .bad
    jmp w16_i21clk                  ; 2Ah, 2Ch (and 2Bh, refused there)
.bad:
    mov ax, 1                       ; invalid function
.err:
    or word [bp + W16_IRET_FL], 1
.done:
    pop bp
    iret

.ver:                               ; DOS 3.30, the version Windows 3.0 wanted
    mov ax, 0x1E03                  ; AL = 3, AH = 30
    xor bx, bx
    xor cx, cx
    jmp short .done

.write:                             ; stdout/stderr have nowhere to go: a
    cmp bx, 4                       ; Windows program has no console. CX bytes
    ja .badh                        ; "written"
    mov ax, cx
    jmp short .done
.close:
    cmp bx, 4
    ja .badh
    jmp short .done
.ioctl:                             ; AL=0 get device info: a character
    cmp bx, 4                       ; device, which is what the runtime asks
    ja .badh                        ; to decide whether a handle is a console
    or al, al
    jne .bad
    mov dx, 0x80D3
    jmp short .done
.badh:
    mov ax, 6                       ; invalid handle
    jmp short .err

; AH=25h/35h keep the program's vectors in a table of our own and never touch
; the machine's: the program sets int 0 (divide error) and the x87 vectors
; for its own runtime, and the real table belongs to the whole machine. The
; table holds eight; a ninth is refused.
.setv:                              ; AL = the vector, DS:DX = the handler
    push bx
    push si
    mov bx, ds                      ; the handler's segment is the caller's DS
    push cs
    pop ds
    call w16_vecslot                ; SI = the row (DS = ours)
    jc .vfull
    mov [si], al
    mov [si + 1], dx
    mov [si + 3], bx
    mov ds, bx                      ; the program's DS back
    pop si
    pop bx
    jmp short .done
.vfull:
    mov ds, bx
    pop si
    pop bx
    mov ax, 8                       ; insufficient memory
    jmp short .err
.getv:                              ; AL = the vector; out ES:BX
    push ds
    push si
    push cs
    pop ds
    call w16_vecslot
    jc .real
    cmp word [si + 3], 0            ; a free row: not one the program set
    je .real
    mov bx, [si + 1]
    mov es, [si + 3]
    pop si
    pop ds
    jmp short .done
.real:
    pop si
    pop ds
    push ax                         ; not one of the program's: the machine's
    xor ah, ah
    shl ax, 1
    shl ax, 1
    mov bx, ax
    xor ax, ax
    mov es, ax
    les bx, [es:bx]
    pop ax
    jmp .done

; w16_vecslot - SI -> AL's row in [w16_vecs], else the first free row (its
; number byte 0 and its pointer word 0); CF=1 = full. DS = ours.
w16_vecslot:
    push cx
    mov si, w16_vecs
    mov cx, 8
.find:
    cmp [si], al
    jne .next
    cmp word [si + 3], 0            ; a used row (vector 0 is a legal number)
    jne .hit
.next:
    add si, 5
    loop .find
    mov si, w16_vecs                ; ...not there: the first empty row
    mov cx, 8
.free:
    cmp word [si + 3], 0
    je .hit
    add si, 5
    loop .free
    pop cx
    stc
    ret
.hit:
    pop cx
    clc
    ret

; AH=4Ch: the program is over. Bank its exit code, put int 21h back and go
; home, as w16_gate does for a waiting call - except that nothing will ever
; finish this one.
w16_i21_exit:
    ; STKBALANCE-OK: AH=4Ch abandons the program's stack for the host's and
    ; returns from w16_resume through w16_tohost
    push cs
    pop ds
    mov [w16_exit], al
    mov byte [w16_state], W16S_ENDED
    call w16_i21_off                ; IF = 0 (the int cleared it)
    mov ss, [w16_hss]
    mov sp, [w16_hsp]
    push word [w16_hflags]
    popf
    cld
    jmp w16_tohost

; -----------------------------------------------------------------------------
; w16_i21clk - int 21h's clock (SPEC.md 124.17.3), off OSAPI_CLK_GET: w16_i21's
; arm, BP pushed, left through its .done
; -----------------------------------------------------------------------------
w16_i21clk:
    ; STKBALANCE-OK: w16_i21's arm, which pops BP and irets in .done
    cmp ah, 0x2B
    je w16_i21.bad                  ; setting the date is not a program's
    cmp ah, 0x2A
    je .date
    push ax                         ; 2Ch: CH hour, CL minute, DH second,
    push bx                         ;   DL hundredths
    push si
    push ds
    mov si, cs
    mov ds, si
    call OSAPI_CLK_GET              ; BX = hour:minute, AH = the second
    push es
    xor si, si
    mov es, si
    mov si, [es:0x46C]              ; SI = the BIOS's tick, which IRQ 0 moves
    pop es
    cmp bx, [w16_ckhm]              ; the kernel's fields are the UI task's,
    jne .anchor                     ;   which is this program while it spins:
    cmp ah, [w16_cks]               ;   they stand still. So the answer is
    je .since                       ;   them, plus the ticks since they last
.anchor:                            ;   moved (SPEC.md 124.17.3)
    mov [w16_ckhm], bx
    mov [w16_cks], ah
    mov [w16_ckt], si
.since:
    mov ax, si
    sub ax, [w16_ckt]               ; ticks since, in hundredths: x 1000 / 182
    cmp ax, 4000                    ; (past three minutes the UI task has
    jbe .t                          ;   run, or this answer is as good as any)
    mov ax, 4000
.t:
    mov cx, 1000
    mul cx
    mov cx, 182
    div cx                          ; AX = hundredths since the anchor
    mov cl, 100
    div cl                          ; AL = seconds since, AH = hundredths
    mov dl, ah
    xor ah, ah
    add al, [w16_cks]
    adc ah, 0
    mov cx, [w16_ckhm]
.sec:
    cmp ax, 60
    jb .done
    sub ax, 60
    inc cl
    cmp cl, 60
    jb .sec
    mov cl, 0
    inc ch
    cmp ch, 24
    jb .sec
    mov ch, 0
    jmp short .sec
.done:
    mov dh, al
    pop ds
    pop si
    pop bx
    pop ax
    jmp w16_i21.done
.date:                              ; CX year, DH month, DL day, AL weekday
    push bx
    push si
    call OSAPI_CLK_GET
    mov bl, dh                      ; Sakamoto's: (y + y/4 - y/100 + y/400 +
    xor bh, bh                      ;   t[m] + d) mod 7, y one less before
    mov al, [cs:w16_i21wd + bx - 1] ;   March; 0 = Sunday (DS: the program's)
    xor ah, ah
    mov si, cx
    cmp bl, 3
    jae .yr
    dec si
.yr:
    add al, dl
    adc ah, 0
    add ax, si
    push dx
    push cx
    mov cx, ax                      ; CX = the sum so far
    mov ax, si
    mov bx, 4
    xor dx, dx
    div bx
    add cx, ax                      ; + y/4
    mov ax, si
    mov bx, 100
    xor dx, dx
    div bx
    sub cx, ax                      ; - y/100
    shr ax, 1
    shr ax, 1
    add cx, ax                      ; + y/400
    mov ax, cx
    mov bx, 7
    xor dx, dx
    div bx
    mov al, dl
    pop cx
    pop dx
    pop si
    pop bx
    jmp w16_i21.done

w16_i21wd:  db 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4
