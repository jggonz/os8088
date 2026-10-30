; =============================================================================
; os8088 - apps/win16/w16x87.inc
;
; FLOATING POINT (SPEC.md 124.5): what a program's OS fixups become, chosen
; per machine. With a coprocessor the code stays native, `__FPMATH` is the
; one below, and an exception the coprocessor raises is kept by `int 2` and
; reported at the program's next `int 3Dh`. Without one, every site becomes
; `int 34h`-`3Dh`, part W16P_FPU (w16fp.inc) is fetched, and `__FPMATH` and
; the ten vectors are its.
;
; The vectors are the program's only while its code runs, swapped with
; int 21h's (w16_i21_on/off, w16core.inc), so two WIN16s never meet in them.
; =============================================================================

W16FP_NONE  equ 0               ; [w16_fpm]: no floating point
W16FP_EMU   equ 1               ;   the emulator part
W16FP_NAT   equ 2               ;   the coprocessor

W16FP_NVEC  equ 11              ; int 34h..3Dh, then int 2

; -----------------------------------------------------------------------------
; w16x_osfix - one OS fixup at ES:DI (pass 1). CL = its kind (target1);
; [w16_rseg] the segment. A kind this table does not know is left alone, as
; Windows' loader would add nothing for it.
; -----------------------------------------------------------------------------
w16x_osfix:
    push ax
    push bx
    cmp cl, 6
    ja .out
    cmp cl, 1
    jb .out
    call w16_inrange                ; the site's word
    jc .out
    cmp byte [w16_fpm], W16FP_NAT
    jne .emu
    cmp cl, 6                       ; native: only the lone fwait, which is
    jne .out                        ;   where an exception is reported
.emu:
    mov bl, cl
    xor bh, bh
    dec bx
    shl bx, 1
    mov ax, [w16x_fix1 + bx]
    add [es:di], ax
    mov ax, [w16x_fix2 + bx]
    or ax, ax
    jz .out
    inc di                          ; kinds 1-3: the segment byte's word too
    call w16_inrange
    jc .back
    add [es:di], ax
.back:
    dec di
.out:
    pop bx
    pop ax
    ret

w16x_fix1:  dw 0xFE32, 0x0632, 0x0E32, 0x1632, 0x5C32, 0xA23D   ; FIxRQQ
w16x_fix2:  dw 0x4000, 0x8000, 0xC000, 0, 0, 0                  ; FJxRQQ

; -----------------------------------------------------------------------------
; w16x_mode - after pass 0: [w16_fpm] for a program with OS fixups or an
; import of __FPMATH ([w16_nfix]), and the part fetched for the emulator.
; CF=1 SI = a line-1 reason (op_fetch has said why in a toast). Keeps ES.
; -----------------------------------------------------------------------------
w16x_mode:
    mov byte [w16_fpm], W16FP_NONE
    xor ax, ax
    mov [w16x_q], al
    mov [w16x_kept], ax
    mov [w16x_ref], ax
    mov [w16x_hand], ax
    mov [w16x_hand + 2], ax
    cmp word [w16_nfix], 0
    je .ok
    call OSAPI_CPU_INFO             ; AH = CPU_F_*, probed
%ifdef W16_X87_EMU                  ; (a test build: the emulator on any CPU,
    and ah, ~CPU_F_X87              ;   W16_CENSUS_ALWAYS's shape)
%endif
    mov byte [w16_fpm], W16FP_NAT
    test ah, CPU_F_X87
    jnz .ok
    mov byte [w16_fpm], W16FP_EMU
    call w16x_fpin
    jc .no
.ok:
    clc
    ret
.no:
    mov byte [w16_fpm], W16FP_NONE
    mov si, w16_r_nofpu
    stc
    ret

w16_r_nofpu:    db 'WIN16', 39, 's 8087 emulator could not be read for', 0

; w16x_fpin - fetch part W16P_FPU from WIN16's own folder, and point its
; vectors and its end stub. CF=1 it could not be. Keeps every register.
w16x_fpin:
    cmp word [w16_fpseg], 0
    jne .have
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call OSAPI_FILE_HERE            ; where the user is, to be put back
    push dx
    push bx
    mov dx, [w16_home]
    mov bl, [w16_homev]
    call OSAPI_FILE_GOTO_QM
    mov al, W16P_FPU
    call op_fetch                   ; claims, reads, expands; a toast if not
    pop bx
    pop dx
    pushf
    call OSAPI_FILE_GOTO_QM
    popf
    jc .out
    mov al, W16P_FPU
    call op_seg
    mov [w16_fpseg], ax
    mov es, ax                      ; its end stub: the x87 row's gate stub
    mov ax, W16_X87ROW * 6
    add ax, w16_stubs
    mov [es:W16FPH_ENDP], ax
    mov [es:W16FPH_ENDP + 2], cs
    xor di, di                      ; its vectors, int 34h..3Dh, in w16x_new
    mov si, W16FPH_VECS
.v:
    mov ax, [es:si]
    mov [w16x_new + di], ax
    mov [w16x_new + di + 2], es
    add si, 2
    add di, 4
    cmp di, 40
    jb .v
    clc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.have:
    clc
    ret

W16FPH_VECS equ 10              ; the part's header (w16fp.inc)
W16FPH_ENDP equ 30
W16FPH_WHY  equ 38

; w16x_end - the program is over: the part dropped, nothing kept. Keeps
; every register.
w16x_end:
    mov byte [w16_fpm], W16FP_NONE
    cmp word [w16_fpseg], 0
    je .out
    push ax
    mov al, W16P_FPU
    call op_drop
    mov word [w16_fpseg], 0
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; w16x_on / w16x_off - the program's vectors in, the machine's back (with
; int 21h's: IF = 0, DS = ours). Keeps every register.
; -----------------------------------------------------------------------------
w16x_on:
    cmp byte [w16_fpm], W16FP_NONE
    je .ret
    push ax
    push cx
    push si
    push di
    push es
    xor ax, ax
    mov es, ax
    mov si, 0x34 * 4                ; the machine's, kept
    xor di, di
.save:
    mov ax, [es:si]
    mov [w16x_old + di], ax
    mov ax, [es:si + 2]
    mov [w16x_old + di + 2], ax
    add si, 4
    add di, 4
    cmp di, 40
    jb .save
    mov ax, [es:2 * 4]
    mov [w16x_old + 40], ax
    mov ax, [es:2 * 4 + 2]
    mov [w16x_old + 42], ax
    cmp byte [w16_fpm], W16FP_EMU
    jne .nat
    mov si, 0x34 * 4                ; the emulator's ten
    xor di, di
.put:
    mov ax, [w16x_new + di]
    mov [es:si], ax
    mov ax, [w16x_new + di + 2]
    mov [es:si + 2], ax
    add si, 4
    add di, 4
    cmp di, 40
    jb .put
    jmp short .out
.nat:
    mov word [es:0x3D * 4], w16x_n3d    ; native: the fwait, and the NMI
    mov [es:0x3D * 4 + 2], cs
    mov word [es:2 * 4], w16x_n02
    mov [es:2 * 4 + 2], cs
.out:
    pop es
    pop di
    pop si
    pop cx
    pop ax
.ret:
    ret

w16x_off:
    cmp byte [w16_fpm], W16FP_NONE
    je .ret
    push ax
    push si
    push di
    push es
    xor ax, ax
    mov es, ax
    mov si, 0x34 * 4
    xor di, di
.back:
    mov ax, [w16x_old + di]
    mov [es:si], ax
    mov ax, [w16x_old + di + 2]
    mov [es:si + 2], ax
    add si, 4
    add di, 4
    cmp di, 40
    jb .back
    mov ax, [w16x_old + 40]
    mov [es:2 * 4], ax
    mov ax, [w16x_old + 42]
    mov [es:2 * 4 + 2], ax
    pop es
    pop di
    pop si
    pop ax
.ret:
    ret

; -----------------------------------------------------------------------------
; The x87 row (SPEC.md 124.5.1): the emulator met an instruction it lacks and
; jumped to this row's gate stub. The program is over, naming it.
; -----------------------------------------------------------------------------
w16x_lacks:
    push si
    push di
    mov si, w16_r_x87
    mov di, w16_l1
    call w16_strcpy
    push es
    mov es, [w16_fpseg]             ; its name, from the part
    mov si, W16FPH_WHY
.c:
    mov al, [es:si]
    or al, al
    jz .e
    mov [di], al
    inc si
    inc di
    jmp short .c
.e:
    pop es
    mov word [di], ','
    mov si, w16_r_x87no
    jmp w16_endself

w16_r_x87:      db 'Needs the x87', 39, 's ', 0
w16_r_x87no:    db 'which WIN16', 39, 's emulator does not have.', 0

; an import the loader always binds itself: reached through the gate, never
w16x_never:
    xor ax, ax
    ret

; -----------------------------------------------------------------------------
; __FPMATH (WIN87EM.1, SPEC.md 124.5.2) with a coprocessor: bound by the
; loader straight here, far, on the program's stack and DS. BX = the
; function, DX:AX its argument; AX (DX) out, CF = 0.
; -----------------------------------------------------------------------------
w16x_fpmath:
    push ds
    push cs
    pop ds
    cmp bx, 12
    ja .bad
    push bx
    shl bx, 1
    call [cs:w16x_fptab + bx]
    pop bx
    clc
    pop ds
    retf
.bad:
    mov ax, 0xFFFF
    cwd
    clc
    pop ds
    retf

w16x_fptab:
    dw w16x_f0, w16x_f1, w16x_f2, w16x_f3, w16x_f4, w16x_f5, w16x_f6
    dw w16x_f7, w16x_f8, w16x_f9, w16x_f10, w16x_f11, w16x_f12

w16x_f0:
    inc word [w16x_ref]
    call w16x_f1
    xor ax, ax
    ret
w16x_f1:
    fninit
    mov word [w16x_cw], 0x1332
    fldcw [w16x_cw]
    mov word [w16x_kept], 0
    mov byte [w16x_q], 0
    ret
w16x_f2:
    cmp word [w16x_ref], 0
    je .out
    dec word [w16x_ref]
.out:
    ret
w16x_f3:
    mov [w16x_hand], ax
    mov [w16x_hand + 2], dx
    ret
w16x_f4:
    mov [w16x_cw], ax
    fldcw [w16x_cw]
    ret
w16x_f5:
    fnstcw [w16x_cw]
    fwait
    mov ax, [w16x_cw]
    ret
w16x_f6:                            ; frndint in AX's rounding mode
    call w16x_rcin
    fwait
    frndint
    jmp short w16x_rcout
w16x_f7:                            ; fistp dword in AX's mode -> DX:AX
    call w16x_rcin
    fwait
    fistp dword [w16x_env]
    call w16x_rcout
    mov ax, [w16x_env]
    mov dx, [w16x_env + 2]
    ret
w16x_f8:
    fnstsw [w16x_sw]
    fwait
    mov ax, [w16x_sw]
    and ax, 0x3F
    or ax, [w16x_kept]
    and ax, 0x1FFF
    mov [w16x_kept], ax
    ret
w16x_f9:
    fnclex
    xor ax, ax
    mov [w16x_kept], ax
    ret
w16x_f10:                           ; the registers in use, off the tag word
    push cx
    push dx
    fnstenv [w16x_env]              ; (which masks everything on an 8087:
    fwait                           ;   put it straight back)
    fldenv [w16x_env]
    mov dx, [w16x_env + 4]
    xor ax, ax
    mov cx, 8
.t:
    mov bl, dl
    and bl, 3
    cmp bl, 3
    je .e
    inc ax
.e:
    shr dx, 1
    shr dx, 1
    loop .t
    pop dx
    pop cx
    ret
w16x_f11:
    mov ax, 1
    ret
w16x_f12:
    ret

; w16x_rcin - the control word kept, its rounding mode := AX & 0C00h
w16x_rcin:
    fnstcw [w16x_cw]
    fwait
    push ax
    and ax, 0x0C00
    push word [w16x_cw]
    pop word [w16x_cw2]
    and word [w16x_cw], 0xF3FF
    or [w16x_cw], ax
    fldcw [w16x_cw]
    pop ax
    ret
w16x_rcout:
    fldcw [w16x_cw2]
    ret

; -----------------------------------------------------------------------------
; The coprocessor's exceptions (SPEC.md 124.5.3)
; -----------------------------------------------------------------------------

; w16x_code - read the coprocessor's status and control words, keep the
; exceptions, clear them: AL = the MS C code of the first one unmasked, 0
; for none. DS = ours. Keeps every other register.
w16x_code:
    push bx
    fnstcw [w16x_cw]
    fwait
    mov al, [w16x_sw]
    and al, 0x3F
    or [w16x_kept], al
    mov bl, [w16x_cw]
    not bl
    and bl, al
    and bl, 0x3D                    ; never the denormal
    mov al, 0
    jz .clr
    mov al, 0x81
    test bl, 1
    jz .nie
    test byte [w16x_sw], 0x40       ; a 387's stack fault: C1 says which
    jz .clr
    mov al, 0x8B
    test byte [w16x_sw + 1], 0x02
    jz .clr
    mov al, 0x8A
    jmp short .clr
.nie:
    mov al, 0x83
    test bl, 4
    jnz .clr
    mov al, 0x84
    test bl, 8
    jnz .clr
    mov al, 0x85
    test bl, 0x10
    jnz .clr
    mov al, 0x86
.clr:
    fnclex
    pop bx
    ret

; w16x_n02 - int 2 while the program runs: the coprocessor's interrupt (an
; 8087's NMI, a 287's IRQ 13 which the AT BIOS sends here) is kept for the
; next int 3Dh; anything else is the machine's
w16x_n02:
    push ax
    push ds
    push cs
    pop ds
    fnstsw [w16x_sw]
    fwait
    test byte [w16x_sw], 0x80       ; ES: the coprocessor's
    jz .chain
    call w16x_code
    or al, al
    jz .done
    mov [w16x_q], al
.done:
    pop ds
    pop ax
    iret
.chain:
    pop ds
    pop ax
    jmp far [cs:w16x_old + 40]

; w16x_n3d - int 3Dh with a coprocessor: the lone fwait. An exception kept by
; int 2, or still in the status word because the NMI came while the vector
; was the machine's, is reported here, from the program's own context.
w16x_n3d:
    sti
    fwait
    push ax
    push ds
    push cs
    pop ds
    mov al, [w16x_q]
    or al, al
    jnz .have
    fnstsw [w16x_sw]
    fwait
    test byte [w16x_sw], 0x80
    jz .none
    call w16x_code
    or al, al
    jz .none
.have:
    mov byte [w16x_q], 0
    mov ah, [w16x_hand + 3]
    or ah, [w16x_hand + 2]
    jz .none                        ; no handler: nothing is called
    xor ah, ah
    pop ds                          ; the program's DS for the handler
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    call far [cs:w16x_hand]
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    iret
.none:
    pop ds
    pop ax
    iret

; the emulator, part W16P_FPU: its own section at vstart 0, cut off the
; image after the dialog part by tools/os88ovl.py's --second
%include "win16/w16fp.inc"
