; =============================================================================
; os8088 - apps/win16/w16i31.inc
;
; A DPMI 0.9 SUBSET ON INT 31h (SPEC.md 124.31.6): what Windows 3.x's standard
; mode gave a program, over OSAPI_SEG as the selector functions are
; (w16sel.inc). A descriptor a program allocates is a WINDOW of WIN16's
; (SEG_WIN): one at a time, on a paragraph below 1 MB, 64 KB at most. The
; kern_mega arm's alone; hooked with int 21h (w16_i21_on) and entered as an
; int on the program's stack, IF = 0, every register the program's. The
; image keeps only the entry: it moves to the UI task's stack, as the DOS
; forward does (w16_i21dos) - kern_mega's own code reads its tables through
; SS - and SCALLs the functions, which are the system-library part's
; (124.24.1). LSL and LAR are 286 instructions: the arm runs on nothing
; older.
; =============================================================================

W16_I31FL   equ 8                   ; [bp+]: the int's FLAGS, past BP, DS, IP, CS

w16_i31:
    ; STKBALANCE-OK: an int's handler, run on the UI task's stack and left by
    ; iret on the program's, as w16_i21dos is
    push ds
    push bp
    mov bp, sp
    push ax
    W16CSDS ax
    mov ds, ax
    pop ax
    mov [w16_i31ss], ss
    mov [w16_i31sp], sp
    mov ss, [w16_hss]
    mov sp, [w16_hsp]
    push word [w16_hflags]          ; interrupts as the host has them: the
    popf                            ;   part may be read from the disk
    cld
    SCALL w16_i31b                  ; CF = the answer, AX its code
    cli
    mov ss, [w16_i31ss]
    mov sp, [w16_i31sp]
    jc .err
    and byte [bp + W16_I31FL], 0xFE
    jmp short .out
.err:
    or byte [bp + W16_I31FL], 1
.out:
    pop bp
    pop ds
    iret

section .w16sys

; w16_i31b - the function in AX, every other register the program's: CF=0
; done, CF=1 AX = DPMI's error code
w16_i31b:
    cmp ax, 0x0400
    je .ver
    cmp ax, 0x0500
    je .mem
    cmp ax, 0x0600                  ; lock and unlock: nothing pages
    je .ok
    cmp ax, 0x0601
    je .ok
    or ah, ah
    jnz .bad
    cmp al, 0x0B
    ja .bad
    push bx
    mov bl, al
    xor bh, bh
    shl bx, 1
    mov ax, [cs:w16_i31tab + bx]    ; XSEC-OK: the part's own table, at cs:
    pop bx
    jmp ax
.bad:
    mov ax, 0x8001                  ; unsupported function
.err:
    stc
    ret
.ok:
    clc
    ret

.alloc:                             ; 0000h: CX = 1 -> AX = a descriptor
    cmp cx, 1
    jne .none
    push cx
    push dx
    push di
    xor dx, dx
    xor di, di
    mov cx, 16
    xor ax, ax
    call w16_i31win
    pop di
    pop dx
    pop cx
    jnc .ok
.none:
    mov ax, 0x8011                  ; descriptor unavailable
    jmp .err

.free:                              ; 0001h: BX, when the program made it
    push ax
    mov ax, bx
    call w16q_wfree
    pop ax
    jmp .ok

.incr:                              ; 0003h: AX = the selector increment
    mov ax, 8
    jmp .ok

.getb:                              ; 0006h: BX -> CX:DX = its base
    push ax
    push bx
    mov dx, bx
    mov bl, SEG_LIN
    call OSAPI_SEG                  ; DX:AX
    mov cx, dx
    mov dx, ax
    pop bx
    pop ax
    jnc .ok
.inval:
    mov ax, 0x8022                  ; invalid selector
    jmp .err

.setb:                              ; 0007h: BX, CX:DX = the base
    push ax
    push cx
    push dx
    push di
    mov di, cx                      ; DI:DX = it, any linear base
cpu 286
    lsl cx, bx                      ; its reach kept
cpu 8086
    inc cx
    mov ax, bx
    call w16_i31win
    pop di
    pop dx
    pop cx
    pop ax
    jnc .ok
    mov ax, 0x8025                  ; invalid linear address
    jmp .err

.setl:                              ; 0008h: BX, CX:DX = the limit
    push ax
    push cx
    push dx
    or cx, cx
    mov cx, dx
    jz .setl1
    mov cx, 0xFFFF                  ; past 64 KB: 64 KB
.setl1:
    inc cx                          ; bytes, 0 = 64 KB
    push bx
    mov dx, bx
    mov bl, SEG_LIN
    call OSAPI_SEG                  ; DX:AX = its base, aimed at again
    pop bx
    jc .setlno
    push di
    mov di, dx
    mov dx, ax                      ; DI:DX = it
    mov ax, bx
    call w16_i31win
    pop di
    jc .setlno
    pop dx
    pop cx
    pop ax
    jmp .ok
.setlno:
    pop dx
    pop cx
    pop ax
    jmp .inval

.alias:                             ; 000Ah: BX -> AX = a data alias
    push bx
    push cx
    push dx
    mov dx, bx
    xor ax, ax
    xor cx, cx
    mov bl, SEG_SUB
    call OSAPI_SEG
    mov ax, dx
    pop dx
    pop cx
    pop bx
    jc .inval
    jmp .ok

.desc:                              ; 000Bh: BX -> ES:DI, its 8 bytes
    push ax
    push cx
    push dx
    push bx
    mov dx, bx
    mov bl, SEG_LIN
    call OSAPI_SEG                  ; DX:AX = its base
    pop bx
    jc .descno
    mov [es:di + 2], ax
    mov [es:di + 4], dl
    mov [es:di + 7], dh
cpu 286
    lsl cx, bx
    lar ax, bx
cpu 8086
    mov [es:di], cx
    mov [es:di + 5], ah
    mov byte [es:di + 6], 0
    pop dx
    pop cx
    pop ax
    jmp .ok
.descno:
    pop dx
    pop cx
    pop ax
    jmp .inval

.ver:                               ; 0400h: 0.90; CL a 286 or a 386; the
    call OSAPI_CPU_INFO             ;   real-mode PIC bases
    mov cl, 2
    cmp al, CPU_286
    jbe .ver1
    mov cl, 3
.ver1:
    mov ax, 0x005A
    xor bx, bx
    mov dx, 0x0870
    jmp .ok

.mem:                               ; 0500h: ES:DI, 30h bytes: the largest
    push ax                         ;   free block, the rest unknown (-1)
    push bx
    push cx
    push dx
    push di
    mov ax, 0xFFFF
    mov cx, 0x18
    cld
    rep stosw
    pop di
    MEMKB_AVAIL                     ; AX = the largest free run, KB
    xor dx, dx
    mov cx, 10
.kb:
    shl ax, 1
    rcl dx, 1
    loop .kb
    mov [es:di], ax
    mov [es:di + 2], dx
    pop dx
    pop cx
    pop bx
    pop ax
    jmp .ok

; w16_i31win - DI:DX = a linear base, CX = bytes (0 = 64 KB), AX = a window the
; program made to re-aim, or 0 -> CF=0 AX = the window; CF=1 none
; (w16q_win's). Keeps the rest.
w16_i31win:
    call w16q_win
    cmp ax, 1                       ; CF=1: AX = 0
    ret

w16_i31tab:
    dw w16_i31b.alloc, w16_i31b.free, w16_i31b.bad, w16_i31b.incr
    dw w16_i31b.bad, w16_i31b.bad, w16_i31b.getb, w16_i31b.setb
    dw w16_i31b.setl, w16_i31b.bad, w16_i31b.alias, w16_i31b.desc

section .text
