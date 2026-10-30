; =============================================================================
; os8088 - apps/win16/w16fp.inc
;
; THE 8087 EMULATOR (SPEC.md 124.5.1): part W16P_FPU of WIN16.O88, fetched
; when a program carries OS fixups and the machine has no coprocessor. The
; loader has rewritten every `wait`+ESC into `int 34h`-`3Bh` (`3Ch` with a
; segment byte first, `3Dh` a bare `fwait`), and these are the handlers.
;
; IT IS SELF-CONTAINED: its registers, status and control words live in its
; own segment, and it calls nothing of WIN16's but the two far pointers in
; its header (the end stub, the runtime's error handler). So the host oracle
; (tests/unit/t_w16fp.py) loads build/win16/w16fp.bin into unicorn and runs
; it against the CPU's own x87, with nothing of WIN16 around it.
;
; It runs on the PROGRAM's stack with DS = its own segment: a far call to the
; C runtime's floating-point error handler can longjmp straight out of it.
;
; A VALUE in work is unpacked: an 80-bit mantissa (five words, the integer
; bit at the top of the fifth, sixteen guard bits in the first), an exponent
; (a signed word, the value being 1.m x 2^e), a sign byte and a class. Every
; result is normalised, then rounded once by w16f_rnd to the precision and
; range of where it goes - a register (the control word's precision, the
; extended range), an m64 or an m32 - which is the 8087's double rounding.
; =============================================================================

section .w16fp vstart=0 align=1

; --- the header WIN16 reads (win16.asm names these offsets) -------------------
w16f_base:
    jmp short w16f_go               ; +0  far: __FPMATH's emulator half
    db 'W16X87EM'                   ; +2  tools/os88ovl.py's cut mark
w16f_vecs:                          ; +10 int 34h..3Dh
    dw w16f_i34, w16f_i35, w16f_i36, w16f_i37
    dw w16f_i38, w16f_i39, w16f_i3a, w16f_i3b
    dw w16f_i3c, w16f_i3d
w16f_endp:  dd 0                    ; +30 WIN16's x87 stub: the program ends
w16f_hand:  dd 0                    ; +34 the runtime's handler (__FPMATH 3)
w16f_why:   times 12 db 0           ; +38 what it lacked, NUL-terminated
w16f_go:    jmp w16f_fpmath         ; +50 (+0's short jump lands here)

; --- the operand record ---------------------------------------------------------
W16FO_M     equ 0                   ; five words, m0 (guard) .. m4 (integer bit)
W16FO_E     equ 10                  ; the exponent, signed
W16FO_S     equ 12                  ; the sign: 0 or 80h
W16FO_K     equ 13                  ; the class
W16FO_SIZE  equ 14
FK_ZERO     equ 0
FK_NORM     equ 1
FK_INF      equ 2
FK_NAN      equ 3

FE_IE       equ 0x01                ; the status word's exception bits
FE_DE       equ 0x02
FE_ZE       equ 0x04
FE_OE       equ 0x08
FE_UE       equ 0x10
FE_PE       equ 0x20
FE_UNWR     equ FE_IE | FE_ZE | FE_OE | FE_UE   ; unmasked: no result written

FT_VALID    equ 0                   ; tags
FT_ZERO     equ 1
FT_SPEC     equ 2
FT_EMPTY    equ 3

; --- the 8087 -------------------------------------------------------------------
w16f_cw:    dw 0x037F               ; the control word
w16f_sw:    dw 0                    ; the status word, TOP kept apart
w16f_tp:    db 0                    ; TOP
w16f_tag:   times 8 db FT_EMPTY     ; a tag a PHYSICAL register
w16f_regs:  times 80 db 0           ; the physical registers, Intel's format
w16f_acc:   dw 0                    ; __FPMATH 8's kept exceptions
w16f_ref:   dw 0                    ; __FPMATH 0/2's count

; --- one instruction's state -----------------------------------------------------
w16f_exc:   db 0                    ; what it raised
w16f_nowr:  db 0                    ; 1 = an unmasked exception: write nothing
w16f_sf:    db 0                    ; the stack's fault: 1 under, 2 over
w16f_esc:   db 0                    ; the escape, 0-7
w16f_ovr:   db 0                    ; int 3Ch's segment: 0 none, else 1 + code
w16f_mrm:   db 0                    ; the modrm
w16f_eoff:  dw 0                    ; a memory operand's address
w16f_eseg:  dw 0
w16f_prec:  dw 64                   ; w16f_rnd's precision and range
w16f_emin:  dw -16382
w16f_emax:  dw 16383
w16f_tiny:  db 0
w16f_inx:   db 0
w16f_cnt:   db 0

w16f_a:     times W16FO_SIZE db 0   ; the destination side
w16f_b:     times W16FO_SIZE db 0   ; the source
w16f_t:     times W16FO_SIZE db 0   ; a swap
w16f_p:     times 16 db 0           ; a product, eight words
w16f_qq:    times 12 db 0           ; a quotient or a root, six words
w16f_rr:    times 12 db 0           ; a remainder, six words
w16f_tt:    times 12 db 0           ; sqrt's trial
w16f_mbuf:  times 96 db 0           ; a memory operand (fsave's 94)
w16f_rawb:  times 10 db 0           ; a register's bytes, packed

w16f_indef: dw 0, 0, 0, 0xC000, 0xFFFF    ; the real indefinite, Intel's

; =============================================================================
; __FPMATH, the emulator's half (SPEC.md 124.5.2): far, BX = the function,
; DX:AX its argument. Out AX (DX), CF = 0. Keeps BX, CX, SI, DI, BP, ES.
; =============================================================================
w16f_fpmath:
    push ds
    push cs
    pop ds
    cmp bx, 12
    ja .bad
    push bx
    push cx
    push si
    push di
    push es
    push cs
    pop es
    shl bx, 1
    call [cs:w16f_fptab + bx]
    pop es
    pop di
    pop si
    pop cx
    pop bx
    clc
    pop ds
    retf
.bad:
    or ax, 0xFFFF
    cwd
    clc
    pop ds
    retf

w16f_fptab:
    dw w16f_fp0, w16f_fp1, w16f_fp2, w16f_fp3, w16f_fp4, w16f_fp5
    dw w16f_fp6, w16f_fp7, w16f_fp8, w16f_fp9, w16f_fp10, w16f_fp11
    dw w16f_fp12

w16f_fp0:                           ; start: counted, then reset
    inc word [w16f_ref]
    call w16f_fp1
    xor ax, ax
    ret
w16f_fp1:                           ; reset: finit, 1332h, nothing pending
    call w16f_finit
    mov word [w16f_cw], 0x1332
    mov word [w16f_acc], 0
    ret
w16f_fp2:
    cmp word [w16f_ref], 0
    je .out
    dec word [w16f_ref]
.out:
    ret
w16f_fp3:
    mov [w16f_hand], ax
    mov [w16f_hand + 2], dx
    ret
w16f_fp4:
    mov [w16f_cw], ax
    ret
w16f_fp5:
    mov ax, [w16f_cw]
    ret
w16f_fp6:                           ; st(0) to an integer, in AX's mode
    push word [w16f_cw]
    call w16f_rcmode
    call w16f_x_rndint
    pop word [w16f_cw]
    mov al, [w16f_exc]
    or [w16f_sw], al
    ret
w16f_fp7:                           ; st(0) to a dword, popped, in AX's mode
    push word [w16f_cw]
    call w16f_rcmode
    mov cx, 4
    call w16f_fisto
    mov ax, [w16f_mbuf]
    mov dx, [w16f_mbuf + 2]
    cmp byte [w16f_nowr], 0
    jne .np
    call w16f_pop
.np:
    pop word [w16f_cw]
    push ax
    mov al, [w16f_exc]
    or [w16f_sw], al
    pop ax
    ret
w16f_fp8:
    mov ax, [w16f_sw]
    and ax, 0x3F
    or ax, [w16f_acc]
    and ax, 0x1FFF
    mov [w16f_acc], ax
    ret
w16f_fp9:
    and word [w16f_sw], 0x7F00
    xor ax, ax
    mov [w16f_acc], ax
    ret
w16f_fp10:                          ; the registers in use
    xor ax, ax
    mov si, w16f_tag
    mov cx, 8
.t:
    cmp byte [si], FT_EMPTY
    je .e
    inc ax
.e:
    inc si
    loop .t
    ret
w16f_fp11:
    xor ax, ax
    ret
w16f_fp12:
    ret

; w16f_rcmode - the control word's rounding mode := AX & 0C00h, and this
; instruction's state cleared, for __FPMATH 6 and 7
w16f_rcmode:
    and ax, 0x0C00
    and word [w16f_cw], 0xF3FF
    or [w16f_cw], ax
    xor ax, ax
    mov [w16f_exc], al
    mov [w16f_nowr], al
    mov [w16f_sf], al
    ret

; =============================================================================
; The interrupt handlers: int 34h-3Bh carry the escape's modrm, 3Ch a segment
; byte first, 3Dh nothing (an fwait: there is nothing to wait for)
; =============================================================================
%macro W16FINT 1
    push ax
    mov al, %1
    jmp w16f_int
%endmacro
w16f_i34: W16FINT 0
w16f_i35: W16FINT 1
w16f_i36: W16FINT 2
w16f_i37: W16FINT 3
w16f_i38: W16FINT 4
w16f_i39: W16FINT 5
w16f_i3a: W16FINT 6
w16f_i3b: W16FINT 7
w16f_i3c: W16FINT 8
w16f_i3d:
    iret

; the frame, from BP: the program's registers, then the interrupt's
W16FF_ES    equ 0
W16FF_DS    equ 2
W16FF_BP    equ 4
W16FF_DI    equ 6
W16FF_SI    equ 8
W16FF_DX    equ 10
W16FF_CX    equ 12
W16FF_BX    equ 14
W16FF_AX    equ 16
W16FF_IP    equ 18
W16FF_CS    equ 20

w16f_int:
    ; STKBALANCE-OK: an interrupt handler's frame, shared by the ten stubs
    ; above (each pushed AX), unwound below or by w16f_unsup's far jump
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es
    mov bp, sp
    sti
    cld
    push cs
    pop ds
    mov es, [bp + W16FF_CS]
    mov si, [bp + W16FF_IP]
    mov byte [w16f_ovr], 0
    cmp al, 8
    jb .esc
    mov al, [es:si]                 ; int 3Ch: the segment in the top two
    inc si                          ;   bits, the escape in the low three
    mov ah, al
    mov cl, 6
    shr ah, cl
    inc ah
    mov [w16f_ovr], ah
    and al, 7
.esc:
    mov [w16f_esc], al
    mov dl, [es:si]
    inc si
    mov [w16f_mrm], dl
    mov al, dl
    and al, 0xC0
    cmp al, 0xC0
    je .reg
    call w16f_ea                    ; SI past the displacement
    mov [bp + W16FF_IP], si
    push cs
    pop es
    xor ax, ax
    mov [w16f_exc], al
    mov [w16f_nowr], al
    mov [w16f_sf], al
    mov bl, [w16f_esc]
    xor bh, bh
    mov cl, 3
    shl bx, cl
    mov al, [w16f_mrm]
    mov cl, 3
    shr al, cl
    and al, 7
    or bl, al
    shl bx, 1
    call [w16f_mtab + bx]
    jmp short w16f_post
.reg:
    mov [bp + W16FF_IP], si
    push cs
    pop es
    xor ax, ax
    mov [w16f_exc], al
    mov [w16f_nowr], al
    mov [w16f_sf], al
    mov bl, [w16f_esc]
    xor bh, bh
    mov cl, 3
    shl bx, cl
    mov al, dl
    shr al, cl
    and al, 7
    or bl, al
    shl bx, 1
    mov cl, dl
    and cl, 7                       ; CL = i, of st(i)
    call [w16f_rtab + bx]
    ; fall into w16f_post

; w16f_post - the instruction's exceptions into the status word, and one the
; control word does not mask reported to the C runtime (SPEC.md 124.5.3)
w16f_post:
    mov al, [w16f_exc]
    or [w16f_sw], al
    mov ah, [w16f_cw]
    not ah
    and ah, al
    and ah, 0x3D                    ; never the denormal (WIN87EM's DM)
    jz .done
    or word [w16f_sw], 0x8080       ; ES and B
    mov al, 0x8B                    ; the code, WIN87EM's priority
    test ah, FE_IE
    jz .nie
    cmp byte [w16f_sf], 1
    je .code
    mov al, 0x8A
    cmp byte [w16f_sf], 2
    je .code
    mov al, 0x81
    jmp short .code
.nie:
    mov al, 0x83
    test ah, FE_ZE
    jnz .code
    mov al, 0x84
    test ah, FE_OE
    jnz .code
    mov al, 0x85
    test ah, FE_UE
    jnz .code
    mov al, 0x86
.code:
    mov bx, [w16f_hand]
    or bx, [w16f_hand + 2]
    jz .done
    xor ah, ah
    mov ds, [bp + W16FF_DS]         ; DS as the program left it
    call far [cs:w16f_hand]
.done:
    pop es
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    iret

; w16f_ea - the memory operand's address: [w16f_eoff], [w16f_eseg]. In: DL =
; the modrm, ES:SI past it; out: SI past the displacement
w16f_ea:
    mov al, dl
    and al, 7
    mov bl, dl
    and bl, 0xC0                    ; the mod
    xor di, di                      ; DI = the address
    mov ah, 0                       ; AH = 1: SS is the default
    cmp bl, 0
    jne .base
    cmp al, 6
    jne .base
    mov di, [es:si]                 ; mod 0, rm 6: a disp16 alone
    inc si
    inc si
    jmp short .seg
.base:
    cmp al, 0
    jne .r1
    mov di, [bp + W16FF_BX]
    add di, [bp + W16FF_SI]
    jmp short .disp
.r1:
    cmp al, 1
    jne .r2
    mov di, [bp + W16FF_BX]
    add di, [bp + W16FF_DI]
    jmp short .disp
.r2:
    cmp al, 2
    jne .r3
    mov di, [bp + W16FF_BP]
    add di, [bp + W16FF_SI]
    mov ah, 1
    jmp short .disp
.r3:
    cmp al, 3
    jne .r4
    mov di, [bp + W16FF_BP]
    add di, [bp + W16FF_DI]
    mov ah, 1
    jmp short .disp
.r4:
    cmp al, 4
    jne .r5
    mov di, [bp + W16FF_SI]
    jmp short .disp
.r5:
    cmp al, 5
    jne .r6
    mov di, [bp + W16FF_DI]
    jmp short .disp
.r6:
    cmp al, 6
    jne .r7
    mov di, [bp + W16FF_BP]
    mov ah, 1
    jmp short .disp
.r7:
    mov di, [bp + W16FF_BX]
.disp:
    cmp bl, 0x40
    jne .d16
    mov al, [es:si]
    inc si
    push ax
    cbw
    add di, ax
    pop ax
    jmp short .seg
.d16:
    cmp bl, 0x80
    jne .seg
    add di, [es:si]
    inc si
    inc si
.seg:
    mov [w16f_eoff], di
    mov al, [w16f_ovr]
    or al, al
    jnz .ovr
    mov bx, [bp + W16FF_DS]
    or ah, ah
    jz .set
    mov bx, ss
    jmp short .set
.ovr:
    mov bx, [bp + W16FF_ES]         ; 1 + code: 4 ES, 1 DS, 2 SS, 3 CS
    cmp al, 4
    je .set
    mov bx, [bp + W16FF_DS]
    cmp al, 1
    je .set
    mov bx, ss
    cmp al, 2
    je .set
    mov bx, [bp + W16FF_CS]
.set:
    mov [w16f_eseg], bx
    ret

; w16f_rd / w16f_wr - CX bytes between the operand and w16f_mbuf
w16f_rd:
    push ds
    push es
    push si
    push di
    push cx
    push ds
    pop es
    mov di, w16f_mbuf
    mov si, [w16f_eoff]
    mov ds, [w16f_eseg]
    rep movsb
    pop cx
    pop di
    pop si
    pop es
    pop ds
    ret
w16f_wr:
    cmp byte [w16f_nowr], 0
    jne .out
    push es
    push si
    push di
    push cx
    mov si, w16f_mbuf
    mov di, [w16f_eoff]
    mov es, [w16f_eseg]
    rep movsb
    pop cx
    pop di
    pop si
    pop es
.out:
    ret

; w16f_raise - AL = exceptions raised; an unmasked one but PE stops the write
w16f_raise:
    or [w16f_exc], al
    push ax
    mov ah, [w16f_cw]
    not ah
    and al, ah
    test al, FE_UNWR
    jz .out
    mov byte [w16f_nowr], 1
.out:
    pop ax
    ret

; w16f_unsup - the instruction is not the emulator's: the program ends there,
; through WIN16's x87 stub, as if it had called it (SPEC.md 124.5.1)
w16f_unsup:
    mov al, [w16f_esc]              ; its name, or its bytes
    mov ah, [w16f_mrm]
    mov si, w16f_names
.n:
    cmp word [si], 0
    je .hex
    mov bx, [si]
    cmp bl, al
    jne .next
    mov dl, ah
    cmp dl, 0xC0
    jb .mem
    cmp bh, ah                      ; a register form: the whole byte
    je .found
    jmp short .next
.mem:
    and dl, 0x38                    ; a memory form: reg, against BH | 1
    or dl, 1
    cmp bh, dl
    je .found
.next:
    add si, 10
    jmp short .n
.found:
    add si, 2
    mov di, w16f_why
    mov cx, 8
    rep movsb
    mov byte [di], 0
    jmp short .go
.hex:
    mov di, w16f_why                ; its bytes: "DB E4"
    mov al, [w16f_esc]
    add al, 0xD8
    call w16f_hex2
    mov byte [di], ' '
    inc di
    mov al, [w16f_mrm]
    call w16f_hex2
    mov byte [di], 0
.go:
    mov sp, bp                      ; the program's registers, then WIN16's
    pop es                          ;   stub, entered as a far call from the
    pop ds                          ;   instruction's end
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    jmp far [cs:w16f_endp]

w16f_hex2:                          ; AL as two hex digits at DI
    push ax
    mov cl, 4
    shr al, cl
    call .d
    pop ax
    and al, 15
.d:
    add al, '0'
    cmp al, '9'
    jbe .s
    add al, 7
.s:
    mov [di], al
    inc di
    ret

w16f_names:                         ; escape, modrm (a memory form: reg | 1)
    db 1, 0xF0, 'F2XM1', 0, 0, 0
    db 1, 0xF1, 'FYL2X', 0, 0, 0
    db 1, 0xF2, 'FPTAN', 0, 0, 0
    db 1, 0xF3, 'FPATAN', 0, 0
    db 1, 0xF5, 'FPREM1', 0, 0
    db 1, 0xF9, 'FYL2XP1', 0
    db 1, 0xFB, 'FSINCOS', 0
    db 1, 0xFE, 'FSIN', 0, 0, 0, 0
    db 1, 0xFF, 'FCOS', 0, 0, 0, 0
    db 2, 0xE9, 'FUCOMPP', 0
    db 7, 0x21, 'FBLD', 0, 0, 0, 0
    db 7, 0x31, 'FBSTP', 0, 0, 0
    dw 0

; =============================================================================
; The register stack
; =============================================================================

; w16f_phys - CL = i -> BL = the physical register of st(i), SI = its bytes,
; AL = its tag. Keeps CX, DX
w16f_phys:
    mov bl, [w16f_tp]
    add bl, cl
    and bl, 7
    xor bh, bh
    mov al, [w16f_tag + bx]
    mov si, bx
    shl si, 1
    push si
    shl si, 1
    shl si, 1
    pop bx
    add si, bx                      ; SI = BL * 10
    shr bx, 1
    add si, w16f_regs
    ret

; w16f_ldst - st(CL) unpacked into DI. Empty: the stack's underflow, and the
; indefinite in its place
w16f_ldst:
    push bx
    push si
    call w16f_phys
    cmp al, FT_EMPTY
    jne .ok
    mov byte [w16f_sf], 1
    mov al, FE_IE
    call w16f_raise
    mov si, w16f_indef
.ok:
    call w16f_up80
    pop si
    pop bx
    ret

; w16f_setst - SI (unpacked) into st(CL), unless the write is stopped
w16f_setst:
    cmp byte [w16f_nowr], 0
    jne .out
    push ax
    push bx
    push si
    push di
    push si
    call w16f_phys
    mov di, si
    pop si
    call w16f_pk80                  ; AL = the tag
    mov [w16f_tag + bx], al
    pop di
    pop si
    pop bx
    pop ax
.out:
    ret

; w16f_push - SI (unpacked) onto the stack
w16f_push:
    push ax
    push si
    push di
    mov di, w16f_rawb
    call w16f_pk80
    mov si, w16f_rawb
    call w16f_pushraw
    pop di
    pop si
    pop ax
    ret

; w16f_pushraw - ten bytes at SI, AL their tag, onto the stack. A full one is
; the stack's overflow: the indefinite instead, masked
w16f_pushraw:
    push ax
    push bx
    push cx
    push si
    push di
    mov ah, al
    mov bl, [w16f_tp]
    dec bl
    and bl, 7
    xor bh, bh
    cmp byte [w16f_tag + bx], FT_EMPTY
    je .room
    mov byte [w16f_sf], 2
    mov al, FE_IE
    call w16f_raise
    mov si, w16f_indef
    mov ah, FT_SPEC
.room:
    cmp byte [w16f_nowr], 0
    jne .out
    mov [w16f_tp], bl
    mov cl, 0
    push si
    call w16f_phys
    mov di, si
    pop si
    mov cx, 5
    rep movsw
    mov [w16f_tag + bx], ah
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; w16f_rtag - AL = the tag of the ten bytes at SI
w16f_rtag:
    mov ax, [si + 8]
    and ax, 0x7FFF
    jz .z
    cmp ax, 0x7FFF
    je .sp
    mov al, FT_VALID
    test byte [si + 7], 0x80
    jnz .r
.sp:
    mov al, FT_SPEC
.r:
    ret
.z:
    mov ax, [si]
    or ax, [si + 2]
    or ax, [si + 4]
    or ax, [si + 6]
    mov al, FT_ZERO
    jz .r
    mov al, FT_SPEC
    ret

; w16f_pop - the top register freed and popped, unless the write is stopped
w16f_pop:
    cmp byte [w16f_nowr], 0
    jne .out
    push bx
    mov bl, [w16f_tp]
    xor bh, bh
    mov byte [w16f_tag + bx], FT_EMPTY
    inc bl
    and bl, 7
    mov [w16f_tp], bl
    pop bx
.out:
    ret

; w16f_needst - CL = i: CF=1 st(i) is empty, and the underflow raised
w16f_needst:
    push ax
    push bx
    push si
    call w16f_phys
    cmp al, FT_EMPTY
    clc
    jne .out
    mov byte [w16f_sf], 1
    mov al, FE_IE
    call w16f_raise
    stc
.out:
    pop si
    pop bx
    pop ax
    ret

; =============================================================================
; Unpacking and packing: memory and register formats to and from a value
; =============================================================================

; w16f_up80 - SI = ten bytes (Intel's extended), DI = a value
w16f_up80:
    push ax
    push cx
    xor ax, ax
    mov [di], ax                    ; m0
    mov ax, [si]
    mov [di + 2], ax
    mov ax, [si + 2]
    mov [di + 4], ax
    mov ax, [si + 4]
    mov [di + 6], ax
    mov ax, [si + 6]
    mov [di + 8], ax
    mov ax, [si + 8]
    mov cl, ah
    and cl, 0x80
    mov [di + W16FO_S], cl
    and ax, 0x7FFF
    cmp ax, 0x7FFF
    jne .fin
    mov ax, [di + 8]                ; infinity or NaN
    and ax, 0x7FFF
    or ax, [di + 6]
    or ax, [di + 4]
    or ax, [di + 2]
    mov byte [di + W16FO_K], FK_INF
    jz .out
    mov byte [di + W16FO_K], FK_NAN
    or byte [di + 9], 0x80
    jmp short .out
.fin:
    or ax, ax
    jnz .exp
    mov ax, -16382                  ; a denormal, or zero
    mov [di + W16FO_E], ax
    call w16f_isz
    jz .zero
    mov al, FE_DE
    call w16f_raise
    jmp short .nrm
.exp:
    sub ax, 16383
    mov [di + W16FO_E], ax
    test byte [di + 9], 0x80
    jnz .norm
    call w16f_isz                   ; an unnormal
    jz .zero
.nrm:
    call w16f_norm
    jmp short .out
.norm:
    mov byte [di + W16FO_K], FK_NORM
    jmp short .out
.zero:
    mov byte [di + W16FO_K], FK_ZERO
.out:
    pop cx
    pop ax
    ret

; w16f_isz - ZF=1 if DI's mantissa is all zeros
w16f_isz:
    push ax
    mov ax, [di]
    or ax, [di + 2]
    or ax, [di + 4]
    or ax, [di + 6]
    or ax, [di + 8]
    pop ax
    ret

; w16f_pk80 - SI = a value -> DI = ten bytes; AL = its tag
w16f_pk80:
    push cx
    mov al, [si + W16FO_K]
    cmp al, FK_ZERO
    jne .nz
    xor ax, ax
    mov [di], ax
    mov [di + 2], ax
    mov [di + 4], ax
    mov [di + 6], ax
    mov al, [si + W16FO_S]
    mov ah, al
    xor al, al
    mov [di + 8], ax
    mov al, FT_ZERO
    pop cx
    ret
.nz:
    cmp al, FK_NORM
    je .norm
    mov ax, [si + 2]                ; infinity or NaN: the mantissa as it is
    mov [di], ax
    mov ax, [si + 4]
    mov [di + 2], ax
    mov ax, [si + 6]
    mov [di + 4], ax
    mov ax, [si + 8]
    cmp byte [si + W16FO_K], FK_INF
    jne .nan
    xor ax, ax
    mov [di], ax
    mov [di + 2], ax
    mov [di + 4], ax
    mov ax, 0x8000
.nan:
    or ax, 0x8000
    mov [di + 6], ax
    mov ax, 0x7FFF
    jmp short .se
.norm:
    mov ax, [si + 2]
    mov [di], ax
    mov ax, [si + 4]
    mov [di + 2], ax
    mov ax, [si + 6]
    mov [di + 4], ax
    mov ax, [si + 8]
    mov [di + 6], ax
    xor cx, cx                      ; the biased exponent, 0 for a denormal
    test ah, 0x80
    jz .den
    mov cx, [si + W16FO_E]
    add cx, 16383
.den:
    mov ax, cx
.se:
    or ah, [si + W16FO_S]
    mov [di + 8], ax
    mov al, FT_SPEC                 ; the tag
    cmp byte [si + W16FO_K], FK_NORM
    jne .tag
    test byte [di + 7], 0x80
    jz .tag
    mov al, FT_VALID
.tag:
    pop cx
    ret

; w16f_up64 - w16f_mbuf's double -> DI
w16f_up64:
    push ax
    push bx
    push cx
    push dx
    mov word [di], 0                ; the 64 bits, shifted left 11 into
                                    ;   m1..m4: the fraction under the
                                    ;   integer bit
    mov ax, [w16f_mbuf]
    mov bx, [w16f_mbuf + 2]
    mov cx, [w16f_mbuf + 4]
    mov dx, [w16f_mbuf + 6]
    mov [di + W16FO_S], dh
    and byte [di + W16FO_S], 0x80
    push dx
    and dx, 0x000F                  ; the fraction's top four
    mov [di + 2], ax
    mov [di + 4], bx
    mov [di + 6], cx
    mov [di + 8], dx
    mov cx, 11
.s2:
    shl word [di + 2], 1
    rcl word [di + 4], 1
    rcl word [di + 6], 1
    rcl word [di + 8], 1
    loop .s2
    pop dx
    mov ax, dx
    and ax, 0x7FF0
    mov cl, 4
    shr ax, cl                      ; the biased exponent
    cmp ax, 0x7FF
    jne .fin
    mov byte [di + W16FO_K], FK_INF
    call w16f_isz
    jz .infok
    mov byte [di + W16FO_K], FK_NAN
    test byte [di + 9], 0x40        ; quiet bit (under the integer bit)
    jnz .q
    mov al, FE_IE                   ; a signalling NaN loaded
    call w16f_raise
    or byte [di + 9], 0x40
.q:
    or byte [di + 9], 0x80
    jmp short .out
.infok:
    or byte [di + 9], 0x80
    jmp short .out
.fin:
    or ax, ax
    jnz .exp
    call w16f_isz
    jz .zero
    mov word [di + W16FO_E], -1022  ; a denormal
    mov al, FE_DE
    call w16f_raise
    call w16f_norm
    jmp short .out
.exp:
    sub ax, 1023
    mov [di + W16FO_E], ax
    or byte [di + 9], 0x80
    mov byte [di + W16FO_K], FK_NORM
    jmp short .out
.zero:
    mov byte [di + W16FO_K], FK_ZERO
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16f_up32 - w16f_mbuf's single -> DI
w16f_up32:
    push ax
    push cx
    xor ax, ax
    mov [di], ax
    mov [di + 2], ax
    mov [di + 4], ax
    mov ax, [w16f_mbuf]             ; the fraction, shifted left 8 into m3/m4
    mov cx, [w16f_mbuf + 2]
    mov [di + W16FO_S], ch
    and byte [di + W16FO_S], 0x80
    mov [di + 6], ax
    push cx
    and cx, 0x007F
    mov [di + 8], cx
    mov cx, 8
.s:
    shl word [di + 6], 1
    rcl word [di + 8], 1
    loop .s
    pop ax
    shl ax, 1
    mov al, ah                      ; the biased exponent
    xor ah, ah
    cmp ax, 0xFF
    jne .fin
    mov byte [di + W16FO_K], FK_INF
    call w16f_isz
    jz .inf
    mov byte [di + W16FO_K], FK_NAN
    test byte [di + 9], 0x40
    jnz .inf
    mov al, FE_IE
    call w16f_raise
    or byte [di + 9], 0x40
.inf:
    or byte [di + 9], 0x80
    jmp short .out
.fin:
    or ax, ax
    jnz .exp
    call w16f_isz
    jz .zero
    mov word [di + W16FO_E], -126
    mov al, FE_DE
    call w16f_raise
    call w16f_norm
    jmp short .out
.exp:
    sub ax, 127
    mov [di + W16FO_E], ax
    or byte [di + 9], 0x80
    mov byte [di + W16FO_K], FK_NORM
    jmp short .out
.zero:
    mov byte [di + W16FO_K], FK_ZERO
.out:
    pop cx
    pop ax
    ret

; w16f_upint - w16f_mbuf's integer of CX bytes (2, 4, 8) -> DI
w16f_upint:
    push ax
    push cx
    push si
    xor ax, ax
    mov [di], ax                    ; sign-extended into m1..m4
    mov si, w16f_mbuf
    add si, cx
    mov al, [si - 1]
    cbw
    mov al, ah
    mov [di + 2], ax
    mov [di + 4], ax
    mov [di + 6], ax
    mov [di + 8], ax
    push di
    add di, 2
    mov si, w16f_mbuf
    rep movsb
    pop di
    mov byte [di + W16FO_S], 0
    test byte [di + 9], 0x80
    jz .pos
    mov byte [di + W16FO_S], 0x80   ; the magnitude
    not word [di + 2]
    not word [di + 4]
    not word [di + 6]
    not word [di + 8]
    add word [di + 2], 1
    adc word [di + 4], 0
    adc word [di + 6], 0
    adc word [di + 8], 0
.pos:
    mov word [di + W16FO_E], 63
    call w16f_isz
    jnz .nz
    mov byte [di + W16FO_K], FK_ZERO
    jmp short .out
.nz:
    call w16f_norm
.out:
    pop si
    pop cx
    pop ax
    ret

; w16f_pk64 - SI = a value ROUNDED to a double -> w16f_mbuf
w16f_pk64:
    push ax
    push bx
    push cx
    push dx
    cmp byte [si + W16FO_K], FK_ZERO
    jne .nz
    xor ax, ax
    xor bx, bx
    xor cx, cx
    xor dx, dx
    jmp short .sign
.nz:
    mov ax, [si + 2]
    mov bx, [si + 4]
    mov cx, [si + 6]
    mov dx, [si + 8]
    push si
    mov si, 11
.s2:
    shr dx, 1
    rcr cx, 1
    rcr bx, 1
    rcr ax, 1
    dec si
    jnz .s2
    pop si
    and dx, 0x000F                  ; the integer bit off
    cmp byte [si + W16FO_K], FK_NORM
    jne .spec
    test byte [si + 9], 0x80
    jz .sign                        ; a denormal: exponent 0
    push ax
    mov ax, [si + W16FO_E]
    add ax, 1023
    push cx
    mov cl, 4
    shl ax, cl
    pop cx
    or dx, ax
    pop ax
    jmp short .sign
.spec:
    cmp byte [si + W16FO_K], FK_INF
    jne .nan
    xor ax, ax                      ; infinity: no fraction
    xor bx, bx
    xor cx, cx
    xor dx, dx
.nan:
    or dx, 0x7FF0                   ; ...a NaN keeps its own
.sign:
    mov [w16f_mbuf], ax
    mov [w16f_mbuf + 2], bx
    mov [w16f_mbuf + 4], cx
    mov al, [si + W16FO_S]
    or dh, al
    mov [w16f_mbuf + 6], dx
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16f_pk32 - SI = a value ROUNDED to a single -> w16f_mbuf
w16f_pk32:
    push ax
    push cx
    push dx
    xor ax, ax
    xor dx, dx
    cmp byte [si + W16FO_K], FK_ZERO
    je .sign
    mov ax, [si + 6]                ; m3/m4, down 8: 24 bits
    mov dx, [si + 8]
    mov cx, 8
.s:
    shr dx, 1
    rcr ax, 1
    loop .s
    and dx, 0x007F
    cmp byte [si + W16FO_K], FK_NORM
    jne .spec
    test byte [si + 9], 0x80
    jz .sign
    mov cx, [si + W16FO_E]
    add cx, 127
    push ax
    mov al, cl
    xor ah, ah
    mov cl, 7
    shl ax, cl
    or dx, ax
    pop ax
    jmp short .sign
.spec:
    cmp byte [si + W16FO_K], FK_INF
    jne .nan
    xor ax, ax
    xor dx, dx
.nan:
    or dx, 0x7F80
.sign:
    mov [w16f_mbuf], ax
    mov al, [si + W16FO_S]
    or dh, al
    mov [w16f_mbuf + 2], dx
    pop dx
    pop cx
    pop ax
    ret

; =============================================================================
; Shifting, normalising, rounding
; =============================================================================

; w16f_shr - DI's mantissa right by CX bits; every bit shifted out ORs into
; bit 0 (the sticky bit). Keeps every register
w16f_shr:
    push ax
    push bx
    push cx
    xor bx, bx                      ; BX: was anything shifted out
    cmp cx, 80
    jbe .w
    call w16f_isz
    jz .z
    mov bl, 1
.z:
    xor ax, ax
    mov [di], ax
    mov [di + 2], ax
    mov [di + 4], ax
    mov [di + 6], ax
    mov [di + 8], ax
    jmp short .st
.w:
    cmp cx, 16
    jb .b
    or bx, [di]
    mov ax, [di + 2]
    mov [di], ax
    mov ax, [di + 4]
    mov [di + 2], ax
    mov ax, [di + 6]
    mov [di + 4], ax
    mov ax, [di + 8]
    mov [di + 6], ax
    mov word [di + 8], 0
    sub cx, 16
    jmp short .w
.b:
    jcxz .st
.bl:
    shr word [di + 8], 1
    rcr word [di + 6], 1
    rcr word [di + 4], 1
    rcr word [di + 2], 1
    rcr word [di], 1
    jnc .nc
    or bl, 1
.nc:
    loop .bl
.st:
    or bx, bx
    jz .out
    or byte [di], 1
.out:
    pop cx
    pop bx
    pop ax
    ret

; w16f_shl - DI's mantissa left by CX bits. CF=1 if a one was shifted out.
; Keeps every register
w16f_shl:
    push ax
    push cx
    xor ah, ah                      ; AH: a one went out
.w:
    cmp cx, 16
    jb .b
    cmp word [di + 8], 0
    je .wz
    mov ah, 1
.wz:
    mov al, 0
    push word [di + 6]
    pop word [di + 8]
    push word [di + 4]
    pop word [di + 6]
    push word [di + 2]
    pop word [di + 4]
    push word [di]
    pop word [di + 2]
    mov word [di], 0
    sub cx, 16
    jmp short .w
.b:
    jcxz .out
.bl:
    shl word [di], 1
    rcl word [di + 2], 1
    rcl word [di + 4], 1
    rcl word [di + 6], 1
    rcl word [di + 8], 1
    jnc .nc
    mov ah, 1
.nc:
    loop .bl
.out:
    cmp ah, 1
    cmc                             ; CF = (AH = 1)
    pop cx
    pop ax
    ret

; w16f_norm - DI's mantissa shifted until its top bit is set, the exponent
; down by as much; all zeros is the class zero. Keeps every register
w16f_norm:
    call w16f_isz
    jnz .nz
    mov byte [di + W16FO_K], FK_ZERO
    ret
.nz:
    mov byte [di + W16FO_K], FK_NORM
    push ax
.w:
    cmp word [di + 8], 0
    jne .b
    push word [di + 6]
    pop word [di + 8]
    push word [di + 4]
    pop word [di + 6]
    push word [di + 2]
    pop word [di + 4]
    push word [di]
    pop word [di + 2]
    mov word [di], 0
    sub word [di + W16FO_E], 16
    jmp short .w
.b:
    test byte [di + 9], 0x80
    jnz .out
    shl word [di], 1
    rcl word [di + 2], 1
    rcl word [di + 4], 1
    rcl word [di + 6], 1
    rcl word [di + 8], 1
    dec word [di + W16FO_E]
    jmp short .b
.out:
    pop ax
    ret

; w16f_rndp - round DI's mantissa to its top CX bits (1-64) in the control
; word's mode; [w16f_inx] = 1 if that lost anything. A carry out of the top
; makes it 1.000 and the exponent one more. Keeps every register.
; By word masks, not by shifting: the kept bits' lowest is bit s = 80 - CX
; of the five words, the half bit is s - 1, and everything below it is the
; sticky part - so nothing moves but the one carry.
w16f_rndp:
    push ax
    push bx
    push cx
    push dx
    push si
    mov dx, 80
    sub dx, cx                      ; DX = s, 16..79
    mov cx, dx
    dec cx                          ; the half bit, h = s - 1
    mov bx, cx
    shr bx, 1
    shr bx, 1
    shr bx, 1
    and bx, 0xFFFE                  ; BX = its word's byte offset
    and cl, 15
    mov ax, 1
    shl ax, cl                      ; AX = its mask
    mov si, [di + bx]
    xor ch, ch
    test si, ax
    jz .nh
    mov ch, 1                       ; CH bit 0: the half
.nh:
    dec ax
    and si, ax                      ; SI: the bits under it, that word ...
.sw:
    or bx, bx
    jz .swd
    dec bx
    dec bx
    or si, [di + bx]                ; ...and every word below
    jmp short .sw
.swd:
    or si, si
    jz .ns
    or ch, 2                        ; CH bit 1: the sticky part
.ns:
    mov [w16f_inx], ch
    mov cx, dx                      ; the kept bits' lowest, s
    mov bx, cx
    shr bx, 1
    shr bx, 1
    shr bx, 1
    and bx, 0xFFFE                  ; BX = its word's byte offset
    and cl, 15
    mov ax, 1
    shl ax, cl                      ; AX = its mask
    push ax                         ; everything below it cleared
    dec ax
    not ax
    and [di + bx], ax
    pop ax
    push bx
.cl:
    or bx, bx
    jz .cld
    dec bx
    dec bx
    mov word [di + bx], 0
    jmp short .cl
.cld:
    pop bx
    mov ch, [w16f_inx]
    or ch, ch
    jz .out                         ; exact
    mov cl, [w16f_cw + 1]           ; the rounding mode
    shr cl, 1
    shr cl, 1
    and cl, 3
    jnz .dn
    test ch, 1                      ; nearest: the half, then a tie to even
    jz .out
    test ch, 2
    jnz .up
    test [di + bx], ax
    jz .out
    jmp short .up
.dn:
    cmp cl, 1
    jne .upm
    cmp byte [di + W16FO_S], 0      ; down: away from zero when negative
    je .out
    jmp short .up
.upm:
    cmp cl, 2
    jne .out                        ; chop
    cmp byte [di + W16FO_S], 0      ; up: away from zero when positive
    jne .out
.up:
    add [di + bx], ax
.cy:
    jnc .out
    inc bx
    inc bx
    cmp bx, 10
    jae .over
    add word [di + bx], 1
    jmp short .cy
.over:
    mov word [di + 8], 0x8000       ; 2^P exactly: 1.000, one more
    xor ax, ax
    mov [di], ax
    mov [di + 2], ax
    mov [di + 4], ax
    mov [di + 6], ax
    inc word [di + W16FO_E]
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16f_rnd - DI (a normalised value) rounded for where it goes: [w16f_prec]
; bits, [w16f_emin]..[w16f_emax], gradual underflow below, the overflow's
; masked response above; PE, UE and OE raised as the 8087 raises them
w16f_rnd:
    cmp byte [di + W16FO_K], FK_NORM
    jne .ret
    push ax
    push cx
    mov byte [w16f_tiny], 0
    mov ax, [w16f_emin]
    sub ax, [di + W16FO_E]
    jle .notiny
    mov byte [w16f_tiny], 1         ; denormalise: at emin, shifted down
    mov cx, ax
    cmp cx, 100
    jbe .sh
    mov cx, 100
.sh:
    call w16f_shr
    mov ax, [w16f_emin]
    mov [di + W16FO_E], ax
.notiny:
    mov cx, [w16f_prec]
    call w16f_rndp
    call w16f_isz
    jnz .nz
    mov byte [di + W16FO_K], FK_ZERO
.nz:
    mov al, 0
    cmp byte [w16f_inx], 0
    je .ex
    mov al, FE_PE
.ex:
    cmp byte [w16f_tiny], 0
    je .ovf
    test byte [w16f_cw], FE_UE      ; masked: underflow only if inexact
    jz .uex
    or al, al
    jz .ovf
.uex:
    or al, FE_UE
.ovf:
    mov cx, [di + W16FO_E]
    cmp cx, [w16f_emax]
    jle .raise
    or al, FE_OE
    test byte [w16f_cw], FE_OE
    jz .raise                       ; unmasked: nothing is written anyway
    or al, FE_PE
    call w16f_ovfval
.raise:
    or al, al
    jz .out
    call w16f_raise
.out:
    pop cx
    pop ax
.ret:
    ret

; w16f_ovfval - DI := the masked overflow's answer: an infinity, or the
; largest finite value when the rounding mode points back toward zero
w16f_ovfval:
    push ax
    push cx
    mov al, [w16f_cw + 1]
    shr al, 1
    shr al, 1
    and al, 3
    cmp al, 3                       ; chop: the largest
    je .max
    cmp al, 1
    jne .up
    cmp byte [di + W16FO_S], 0      ; down: the largest when positive
    je .max
    jmp short .inf
.up:
    cmp al, 2
    jne .inf
    cmp byte [di + W16FO_S], 0      ; up: the largest when negative
    jne .max
.inf:
    mov byte [di + W16FO_K], FK_INF
    jmp short .out
.max:
    mov ax, 0xFFFF
    mov [di], ax
    mov [di + 2], ax
    mov [di + 4], ax
    mov [di + 6], ax
    mov [di + 8], ax
    mov ax, [w16f_emax]
    mov [di + W16FO_E], ax
    mov word [di], 0                ; cut to the precision
    mov cx, [w16f_prec]
    push word [w16f_cw]
    or byte [w16f_cw + 1], 0x0C     ; (chop, so it is not rounded up)
    call w16f_rndp
    pop word [w16f_cw]
.out:
    pop cx
    pop ax
    ret

; w16f_rreg - [w16f_prec]/[w16f_emin]/[w16f_emax] for a register result:
; the control word's precision, the extended range
w16f_rreg:
    push ax
    mov al, [w16f_cw + 1]
    and al, 3
    mov word [w16f_prec], 24
    cmp al, 0
    je .r
    mov word [w16f_prec], 53
    cmp al, 2
    je .r
    mov word [w16f_prec], 64
.r:
    mov word [w16f_emin], -16382
    mov word [w16f_emax], 16383
    pop ax
    ret
w16f_r64:                           ; ...for an m64
    mov word [w16f_prec], 53
    mov word [w16f_emin], -1022
    mov word [w16f_emax], 1023
    ret
w16f_r32:                           ; ...for an m32
    mov word [w16f_prec], 24
    mov word [w16f_emin], -126
    mov word [w16f_emax], 127
    ret
w16f_rx:                            ; ...exact, in the extended range
    mov word [w16f_prec], 64
    mov word [w16f_emin], -16382
    mov word [w16f_emax], 16383
    ret

; =============================================================================
; Arithmetic on w16f_a and w16f_b, the answer in w16f_a
; =============================================================================

; w16f_indefa - w16f_a := the indefinite, the invalid operation raised
w16f_indefa:
    push si
    push di
    mov al, FE_IE
    call w16f_raise
    mov si, w16f_indef
    mov di, w16f_a
    call w16f_up80
    pop di
    pop si
    ret

; w16f_nan2 - CF=1 if either of a or b is a NaN: a := the 8087's NaN answer
; (the larger significand, made quiet), the invalid operation raised for a
; signalling one
w16f_nan2:
    mov al, [w16f_a + W16FO_K]
    mov ah, [w16f_b + W16FO_K]
    cmp al, FK_NAN
    je .yes
    cmp ah, FK_NAN
    je .yes
    clc
    ret
.yes:
    push si
    push di
    mov si, w16f_a                  ; a signalling NaN: bit 62 clear
    call .sn
    mov si, w16f_b
    call .sn
    cmp al, FK_NAN
    jne .tb
    cmp ah, FK_NAN
    jne .ta
    mov si, w16f_a                  ; both: the larger significand
    mov di, w16f_b
    call w16f_cmpm
    jae .ta
.tb:
    mov si, w16f_b
    mov di, w16f_a
    mov cx, W16FO_SIZE / 2
    rep movsw
.ta:
    or byte [w16f_a + 9], 0x40
    pop di
    pop si
    stc
    ret
.sn:
    cmp byte [si + W16FO_K], FK_NAN
    jne .snr
    test byte [si + 9], 0x40
    jnz .snr
    push ax
    mov al, FE_IE
    call w16f_raise
    pop ax
.snr:
    ret

; w16f_cmpm - compare SI's mantissa with DI's (unsigned): flags as `cmp`
w16f_cmpm:
    push ax
    mov ax, [si + 8]
    cmp ax, [di + 8]
    jne .out
    mov ax, [si + 6]
    cmp ax, [di + 6]
    jne .out
    mov ax, [si + 4]
    cmp ax, [di + 4]
    jne .out
    mov ax, [si + 2]
    cmp ax, [di + 2]
    jne .out
    mov ax, [si]
    cmp ax, [di]
.out:
    pop ax
    ret

; w16f_swapab - exchange a and b
w16f_swapab:
    push cx
    push si
    push di
    push es
    push ds
    pop es
    mov si, w16f_a
    mov di, w16f_t
    mov cx, W16FO_SIZE / 2
    rep movsw
    mov si, w16f_b
    mov di, w16f_a
    mov cx, W16FO_SIZE / 2
    rep movsw
    mov si, w16f_t
    mov di, w16f_b
    mov cx, W16FO_SIZE / 2
    rep movsw
    pop es
    pop di
    pop si
    pop cx
    ret

; w16f_add - a := a + b, rounded for a register
w16f_add:
    call w16f_nan2
    jnc .num
    ret
.num:
    push ax
    push cx
    push di
    mov al, [w16f_a + W16FO_K]
    mov ah, [w16f_b + W16FO_K]
    cmp al, FK_INF
    je .ainf
    cmp ah, FK_INF
    je .binf
    cmp ah, FK_ZERO
    jne .bnz
    cmp al, FK_ZERO
    jne .rnd                        ; x + 0: x, rounded
    mov al, [w16f_a + W16FO_S]      ; 0 + 0: the sign
    cmp al, [w16f_b + W16FO_S]
    je .out
    mov byte [w16f_a + W16FO_S], 0
    call w16f_rcdown
    jne .out
    mov byte [w16f_a + W16FO_S], 0x80
    jmp .out
.bnz:
    cmp al, FK_ZERO
    jne .both
    call w16f_swapab                ; 0 + y: y, rounded
    jmp .rnd
.ainf:
    cmp ah, FK_INF
    jne .out
    mov al, [w16f_a + W16FO_S]
    cmp al, [w16f_b + W16FO_S]
    je .out
    call w16f_indefa                ; inf - inf
    jmp .out
.binf:
    call w16f_swapab
    jmp .out
.both:
    mov ax, [w16f_a + W16FO_E]      ; |a| >= |b|, or swap
    cmp ax, [w16f_b + W16FO_E]
    jg .ord
    jl .sw
    push si
    mov si, w16f_a
    mov di, w16f_b
    call w16f_cmpm
    pop si
    jae .ord
.sw:
    call w16f_swapab
.ord:
    mov cx, [w16f_a + W16FO_E]
    sub cx, [w16f_b + W16FO_E]
    mov di, w16f_b
    call w16f_shr
    mov al, [w16f_a + W16FO_S]
    cmp al, [w16f_b + W16FO_S]
    jne .sub
    mov ax, [w16f_b]                ; the same sign: add
    add [w16f_a], ax
    mov ax, [w16f_b + 2]
    adc [w16f_a + 2], ax
    mov ax, [w16f_b + 4]
    adc [w16f_a + 4], ax
    mov ax, [w16f_b + 6]
    adc [w16f_a + 6], ax
    mov ax, [w16f_b + 8]
    adc [w16f_a + 8], ax
    jnc .rnd
    mov di, w16f_a
    rcr word [di + 8], 1            ; the carry back in at the top, and the
    rcr word [di + 6], 1            ; bit that leaves kept as sticky
    rcr word [di + 4], 1
    rcr word [di + 2], 1
    rcr word [di], 1
    jnc .c
    or byte [di], 1
.c:
    inc word [di + W16FO_E]
    jmp short .rnd
.sub:
    mov ax, [w16f_b]
    sub [w16f_a], ax
    mov ax, [w16f_b + 2]
    sbb [w16f_a + 2], ax
    mov ax, [w16f_b + 4]
    sbb [w16f_a + 4], ax
    mov ax, [w16f_b + 6]
    sbb [w16f_a + 6], ax
    mov ax, [w16f_b + 8]
    sbb [w16f_a + 8], ax
    mov di, w16f_a
    call w16f_norm
    cmp byte [di + W16FO_K], FK_ZERO
    jne .rnd
    mov byte [di + W16FO_S], 0      ; x - x: +0, -0 rounding down
    call w16f_rcdown
    jne .out
    mov byte [di + W16FO_S], 0x80
    jmp short .out
.rnd:
    mov di, w16f_a
    call w16f_rreg
    call w16f_rnd
.out:
    pop di
    pop cx
    pop ax
    ret

; w16f_rcdown - ZF=1 if the rounding mode is down
w16f_rcdown:
    push ax
    mov al, [w16f_cw + 1]
    and al, 0x0C
    cmp al, 0x04
    pop ax
    ret

; w16f_sexp - AX = a's exponent + DX (or - DX with CF=1 in), saturated
w16f_eadd:
    add ax, dx
    jno .ok
    mov ax, 32000
    or dx, dx
    jns .ok
    mov ax, -32000
.ok:
    ret
w16f_esub:
    sub ax, dx
    jno .ok
    mov ax, 32000
    or dx, dx
    js .ok
    mov ax, -32000
.ok:
    ret

; w16f_mul - a := a x b, rounded for a register
w16f_mul:
    call w16f_nan2
    jnc .num
    ret
.num:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, [w16f_b + W16FO_S]
    xor [w16f_a + W16FO_S], al
    mov al, [w16f_a + W16FO_K]
    mov ah, [w16f_b + W16FO_K]
    cmp al, FK_INF
    je .inf
    cmp ah, FK_INF
    je .inf
    cmp al, FK_ZERO
    je .out
    cmp ah, FK_ZERO
    jne .both
    mov byte [w16f_a + W16FO_K], FK_ZERO
    jmp .out
.inf:
    cmp al, FK_ZERO                 ; inf x 0
    je .bad
    cmp ah, FK_ZERO
    je .bad
    mov byte [w16f_a + W16FO_K], FK_INF
    jmp .out
.bad:
    mov al, [w16f_a + W16FO_S]
    call w16f_indefa
    jmp .out
.both:
    mov ax, [w16f_a + W16FO_E]
    mov dx, [w16f_b + W16FO_E]
    call w16f_eadd
    mov [w16f_a + W16FO_E], ax
    xor ax, ax                      ; the product: eight words
    mov di, w16f_p
    mov cx, 8
.z:
    mov [di], ax
    inc di
    inc di
    loop .z
    xor si, si                      ; SI = i (bytes), BX = j
.i:
    xor bx, bx
.j:
    mov ax, [w16f_a + 2 + si]
    mul word [w16f_b + 2 + bx]
    mov di, si
    add di, bx
    add [w16f_p + di], ax
    adc [w16f_p + 2 + di], dx
    jnc .nc
    add di, 4
.cy:
    cmp di, 16
    jae .nc
    add word [w16f_p + di], 1
    jnc .nc
    inc di
    inc di
    jmp short .cy
.nc:
    add bx, 2
    cmp bx, 8
    jb .j
    add si, 2
    cmp si, 8
    jb .i
    test byte [w16f_p + 15], 0x80
    jz .sh
    inc word [w16f_a + W16FO_E]
    jmp short .take
.sh:
    mov di, w16f_p                  ; one left
    clc
    mov cx, 8
.sl:
    rcl word [di], 1
    inc di
    inc di
    loop .sl
.take:
    mov ax, [w16f_p + 6]            ; the top five words; the rest is sticky
    mov [w16f_a], ax
    mov ax, [w16f_p + 8]
    mov [w16f_a + 2], ax
    mov ax, [w16f_p + 10]
    mov [w16f_a + 4], ax
    mov ax, [w16f_p + 12]
    mov [w16f_a + 6], ax
    mov ax, [w16f_p + 14]
    mov [w16f_a + 8], ax
    mov ax, [w16f_p]
    or ax, [w16f_p + 2]
    or ax, [w16f_p + 4]
    jz .rnd
    or byte [w16f_a], 1
.rnd:
    mov di, w16f_a
    call w16f_rreg
    call w16f_rnd
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16f_div - a := a / b, rounded for a register
w16f_div:
    call w16f_nan2
    jnc .num
    ret
.num:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, [w16f_b + W16FO_S]
    xor [w16f_a + W16FO_S], al
    mov al, [w16f_a + W16FO_K]
    mov ah, [w16f_b + W16FO_K]
    cmp al, FK_INF
    jne .af
    cmp ah, FK_INF                  ; inf / inf
    je .bad
    jmp .out                        ; inf / x
.af:
    cmp ah, FK_INF
    jne .bf
    mov byte [w16f_a + W16FO_K], FK_ZERO    ; x / inf
    jmp .out
.bf:
    cmp ah, FK_ZERO
    jne .bnz
    cmp al, FK_ZERO                 ; 0 / 0
    je .bad
    mov al, FE_ZE                   ; x / 0
    call w16f_raise
    mov byte [w16f_a + W16FO_K], FK_INF
    jmp .out
.bad:
    call w16f_indefa
    jmp .out
.bnz:
    cmp al, FK_ZERO
    je .out
    mov ax, [w16f_a + W16FO_E]
    mov dx, [w16f_b + W16FO_E]
    call w16f_esub
    mov [w16f_a + W16FO_E], ax
    ; a's 64 bits over b's, 80 quotient bits, the remainder in DX:CX:BX:AX
    ; and its 65th bit in CF between steps - a restoring division with
    ; nothing in memory but b and the words it has finished. q0 is the
    ; units bit: A/B is in [1/2, 2), so it is 0 at most once, and w16f_norm
    ; then moves everything up one. A remainder left over is sticky.
    push bp
    mov ax, [w16f_a + 2]
    mov bx, [w16f_a + 4]
    mov cx, [w16f_a + 6]
    mov dx, [w16f_a + 8]
    mov di, w16f_qq + 8             ; the quotient, top word first
    mov byte [w16f_cnt], 5
    clc
.word:
    mov si, 16
.bit:
    jc .sub                         ; a 65th bit: R is past B
    cmp dx, [w16f_b + 8]
    jne .cmpd
    cmp cx, [w16f_b + 6]
    jne .cmpd
    cmp bx, [w16f_b + 4]
    jne .cmpd
    cmp ax, [w16f_b + 2]
.cmpd:
    jb .zero
.sub:
    sub ax, [w16f_b + 2]
    sbb bx, [w16f_b + 4]
    sbb cx, [w16f_b + 6]
    sbb dx, [w16f_b + 8]
    stc
    jmp short .q
.zero:
    clc
.q:
    rcl bp, 1
    shl ax, 1                       ; R <<= 1, its top bit to CF
    rcl bx, 1
    rcl cx, 1
    rcl dx, 1
    dec si                          ; (dec, not sub: CF carries on)
    jnz .bit
    mov [di], bp
    dec di
    dec di
    dec byte [w16f_cnt]
    jnz .word
    pop bp
    jc .stk                         ; what is left: sticky
    or ax, bx
    or ax, cx
    or ax, dx
    jz .q0
.stk:
    or byte [w16f_qq], 1
.q0:
    mov si, w16f_qq
    mov di, w16f_a
    push es
    push ds
    pop es
    mov cx, 5
    rep movsw
    pop es
    mov di, w16f_a
    call w16f_norm                  ; q0 = 0: one place up, one less
    call w16f_rreg
    call w16f_rnd
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16f_cmprb - compare R (w16f_rr, five words) with b's 64 bits: flags as cmp
w16f_cmprb:
    push ax
    mov ax, [w16f_rr + 8]
    cmp ax, 0
    jne .out
    mov ax, [w16f_rr + 6]
    cmp ax, [w16f_b + 8]
    jne .out
    mov ax, [w16f_rr + 4]
    cmp ax, [w16f_b + 6]
    jne .out
    mov ax, [w16f_rr + 2]
    cmp ax, [w16f_b + 4]
    jne .out
    mov ax, [w16f_rr]
    cmp ax, [w16f_b + 2]
.out:
    pop ax
    ret
; w16f_subrb - R -= b's 64 bits
w16f_subrb:
    push ax
    mov ax, [w16f_b + 2]
    sub [w16f_rr], ax
    mov ax, [w16f_b + 4]
    sbb [w16f_rr + 2], ax
    mov ax, [w16f_b + 6]
    sbb [w16f_rr + 4], ax
    mov ax, [w16f_b + 8]
    sbb [w16f_rr + 6], ax
    sbb word [w16f_rr + 8], 0
    pop ax
    ret
; w16f_shlr - R <<= 1
w16f_shlr:
    shl word [w16f_rr], 1
    rcl word [w16f_rr + 2], 1
    rcl word [w16f_rr + 4], 1
    rcl word [w16f_rr + 6], 1
    rcl word [w16f_rr + 8], 1
    ret

; w16f_sqrt - a := the square root of a, rounded for a register
w16f_sqrt:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, [w16f_a + W16FO_K]
    cmp al, FK_NAN
    jne .nn
    test byte [w16f_a + 9], 0x40
    jnz .out
    mov al, FE_IE
    call w16f_raise
    or byte [w16f_a + 9], 0x40
    jmp .out
.nn:
    cmp al, FK_ZERO
    je .out                         ; +-0
    cmp byte [w16f_a + W16FO_S], 0
    je .pos
    call w16f_indefa                ; a negative
    jmp .out
.pos:
    cmp al, FK_INF
    je .out
    ; X (w16f_a's mantissa, the top bit set) is the radicand's stream: shifted
    ; right one when the exponent is even, so the pairs fall right
    mov di, w16f_a
    mov ax, [di + W16FO_E]
    test al, 1
    jnz .odd
    mov cx, 1
    call w16f_shr
.odd:
    sar ax, 1                       ; the root's exponent: e / 2, down
    mov [w16f_t + W16FO_E], ax
    xor ax, ax                      ; Q (w16f_qq) and R (w16f_rr): six words
    mov di, w16f_qq
    mov cx, 12
.z:
    mov [di], ax
    inc di
    inc di
    loop .z
    mov cx, 80
.bit:
    push cx
    mov cx, 2                       ; R = R << 2 | the stream's next two
.two:
    shl word [w16f_a], 1
    rcl word [w16f_a + 2], 1
    rcl word [w16f_a + 4], 1
    rcl word [w16f_a + 6], 1
    rcl word [w16f_a + 8], 1
    rcl word [w16f_rr], 1
    rcl word [w16f_rr + 2], 1
    rcl word [w16f_rr + 4], 1
    rcl word [w16f_rr + 6], 1
    rcl word [w16f_rr + 8], 1
    rcl word [w16f_rr + 10], 1
    loop .two
    mov si, w16f_qq                 ; T = Q << 2 | 1
    mov di, w16f_tt
    push es
    push ds
    pop es
    mov cx, 6
    rep movsw
    pop es
    mov cx, 2
.t2:
    shl word [w16f_tt], 1
    rcl word [w16f_tt + 2], 1
    rcl word [w16f_tt + 4], 1
    rcl word [w16f_tt + 6], 1
    rcl word [w16f_tt + 8], 1
    rcl word [w16f_tt + 10], 1
    loop .t2
    or byte [w16f_tt], 1
    mov si, 10                      ; R >= T?
.cmp:
    mov ax, [w16f_rr + si]
    cmp ax, [w16f_tt + si]
    jne .dec
    sub si, 2
    jnc .cmp
    jmp short .ge                   ; equal
.dec:
    jb .lt
.ge:
    mov ax, [w16f_tt]               ; R -= T; Q = Q << 1 | 1
    sub [w16f_rr], ax
    mov ax, [w16f_tt + 2]
    sbb [w16f_rr + 2], ax
    mov ax, [w16f_tt + 4]
    sbb [w16f_rr + 4], ax
    mov ax, [w16f_tt + 6]
    sbb [w16f_rr + 6], ax
    mov ax, [w16f_tt + 8]
    sbb [w16f_rr + 8], ax
    mov ax, [w16f_tt + 10]
    sbb [w16f_rr + 10], ax
    stc
    jmp short .qs
.lt:
    clc
.qs:
    rcl word [w16f_qq], 1
    rcl word [w16f_qq + 2], 1
    rcl word [w16f_qq + 4], 1
    rcl word [w16f_qq + 6], 1
    rcl word [w16f_qq + 8], 1
    rcl word [w16f_qq + 10], 1
    pop cx
    dec cx
    jz .done
    jmp .bit
.done:
    mov si, w16f_qq                 ; the root, five words; a remainder sticky
    mov di, w16f_a
    push es
    push ds
    pop es
    mov cx, 5
    rep movsw
    pop es
    mov ax, [w16f_rr]
    or ax, [w16f_rr + 2]
    or ax, [w16f_rr + 4]
    or ax, [w16f_rr + 6]
    or ax, [w16f_rr + 8]
    or ax, [w16f_rr + 10]
    jz .ex
    or byte [w16f_a], 1
.ex:
    mov ax, [w16f_t + W16FO_E]
    mov [w16f_a + W16FO_E], ax
    mov di, w16f_a
    call w16f_norm
    call w16f_rreg
    call w16f_rnd
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16f_cmp - compare a with b: AH = C3 C2 C0 in the status word's places
; (bits 6, 2, 0 of the high byte), a NaN unordered with the invalid raised
w16f_cmp:
    mov al, [w16f_a + W16FO_K]
    mov ah, [w16f_b + W16FO_K]
    cmp al, FK_NAN
    je .un
    cmp ah, FK_NAN
    jne .ord
.un:
    mov al, FE_IE
    call w16f_raise
    mov ah, 0x45
    ret
.ord:
    push si
    push di
    call w16f_sgn                   ; -1, 0, 1 for a - b
    pop di
    pop si
    mov ah, 0                       ; >
    or al, al
    jz .eq
    js .lt
    ret
.lt:
    mov ah, 0x01
    ret
.eq:
    mov ah, 0x40
    ret

; w16f_sgn - AL = the sign of a - b (both ordered): -1, 0 or 1
w16f_sgn:
    mov al, [w16f_a + W16FO_K]
    mov ah, [w16f_b + W16FO_K]
    cmp al, FK_ZERO
    jne .anz
    cmp ah, FK_ZERO
    jne .bsign                      ; 0 vs b: -sign(b)
    xor al, al
    ret
.bsign:
    mov al, 1
    cmp byte [w16f_b + W16FO_S], 0
    jne .r
    mov al, -1
.r:
    ret
.anz:
    mov dl, [w16f_a + W16FO_S]
    cmp ah, FK_ZERO
    je .asign
    cmp dl, [w16f_b + W16FO_S]
    jne .asign                      ; different signs: a's decides
    ; the same sign: the magnitudes, then the sign flips the answer
    call .mag                       ; AL = -1, 0, 1 for |a| - |b|
    or dl, dl
    jz .r
    neg al
    ret
.asign:
    mov al, 1
    or dl, dl
    jz .r
    mov al, -1
    ret
.mag:
    cmp al, FK_INF
    jne .af
    xor al, al
    cmp ah, FK_INF
    je .mr
    mov al, 1
.mr:
    ret
.af:
    cmp ah, FK_INF
    jne .bf
    mov al, -1
    ret
.bf:
    mov ax, [w16f_a + W16FO_E]
    cmp ax, [w16f_b + W16FO_E]
    jg .g
    jl .l
    mov si, w16f_a
    mov di, w16f_b
    call w16f_cmpm
    ja .g
    jb .l
    xor al, al
    ret
.g:
    mov al, 1
    ret
.l:
    mov al, -1
    ret

; w16f_setcc - AH = C3 C2 C0 (high byte bits 6, 2, 0) into the status word
w16f_setcc:
    and byte [w16f_sw + 1], 0xBA
    or [w16f_sw + 1], ah
    ret

; =============================================================================
; The instructions
; =============================================================================

; w16f_arith - BL = the operation (the reg field: 0 add, 1 mul, 2 com,
; 3 comp, 4 sub, 5 subr, 6 div, 7 divr) on a (the destination side) and b
w16f_arith:
    cmp bl, 2
    je .cmp
    cmp bl, 3
    je .cmp
    cmp bl, 0
    je .add
    cmp bl, 1
    je .mul
    cmp bl, 4
    je .sub
    cmp bl, 5
    je .subr
    cmp bl, 6
    je .div
    call w16f_swapab                ; divr: b / a
.div:
    jmp w16f_div
.subr:
    call w16f_swapab                ; b - a
.sub:
    cmp byte [w16f_b + W16FO_K], FK_NAN
    je .add
    xor byte [w16f_b + W16FO_S], 0x80
.add:
    jmp w16f_add
.mul:
    jmp w16f_mul
.cmp:
    call w16f_cmp
    jmp w16f_setcc

; the memory forms of D8, DA, DC and DE: st(0) := st(0) op the operand
w16f_m_r32:
    mov cx, 4
    call w16f_rd
    mov di, w16f_b
    call w16f_up32
    jmp short w16f_mop
w16f_m_r64:
    mov cx, 8
    call w16f_rd
    mov di, w16f_b
    call w16f_up64
    jmp short w16f_mop
w16f_m_i32:
    mov cx, 4
    jmp short w16f_m_int
w16f_m_i16:
    mov cx, 2
w16f_m_int:
    call w16f_rd
    mov di, w16f_b
    call w16f_upint
w16f_mop:
    mov cl, 0
    mov di, w16f_a
    call w16f_ldst
    mov bl, [w16f_mrm]
    mov cl, 3
    shr bl, cl
    and bl, 7
    push bx
    call w16f_arith
    pop bx
    cmp bl, 2
    je .out
    cmp bl, 3
    je .pop
    mov cl, 0
    mov si, w16f_a
    jmp w16f_setst
.pop:
    jmp w16f_pop
.out:
    ret

; D8 st(i): st(0) := st(0) op st(i)
w16f_r_d8:
    push cx
    mov di, w16f_b
    call w16f_ldst
    mov cl, 0
    mov di, w16f_a
    call w16f_ldst
    pop cx
    mov bl, [w16f_mrm]
    push cx
    mov cl, 3
    shr bl, cl
    pop cx
    and bl, 7
    push bx
    call w16f_arith
    pop bx
    cmp bl, 2
    je .out
    cmp bl, 3
    je .pop
    mov cl, 0
    mov si, w16f_a
    jmp w16f_setst
.pop:
    jmp w16f_pop
.out:
    ret

; DC st(i) / DE st(i): st(i) := st(i) op st(0), the subtractions and
; divisions the other way round (Intel's encoding); DE pops
w16f_r_dc:
    xor dh, dh
    jmp short w16f_rdx
w16f_r_de:
    mov dh, 1
w16f_rdx:
    mov bl, [w16f_mrm]
    push cx
    mov cl, 3
    shr bl, cl
    pop cx
    and bl, 7
    cmp bl, 3
    jne .notcp
    or dh, dh                       ; DE D9 fcompp; DC D8+i fcomp
    jz .cmp
    cmp cl, 1
    je .fcompp
    jmp w16f_unsup
.notcp:
    cmp bl, 2
    je .cmp
    push cx
    push dx
    push bx
    mov di, w16f_a
    call w16f_ldst                  ; a = st(i)
    mov cl, 0
    mov di, w16f_b
    call w16f_ldst                  ; b = st(0)
    pop bx
    cmp bl, 4
    jb .op
    xor bl, 1
.op:
    call w16f_arith
    pop dx
    pop cx
    mov si, w16f_a
    call w16f_setst
    or dh, dh
    jz .out
    jmp w16f_pop
.out:
    ret
.cmp:                               ; fcom/fcomp st(i), the aliases
    push dx
    push bx
    push cx
    mov di, w16f_b
    call w16f_ldst
    mov cl, 0
    mov di, w16f_a
    call w16f_ldst
    call w16f_cmp
    call w16f_setcc
    pop cx
    pop bx
    pop dx
    cmp bl, 3
    je .p1
    or dh, dh
    jz .out
.p1:
    jmp w16f_pop
.fcompp:
    mov cl, 1
    mov di, w16f_b
    call w16f_ldst
    mov cl, 0
    mov di, w16f_a
    call w16f_ldst
    call w16f_cmp
    call w16f_setcc
    call w16f_pop
    jmp w16f_pop

; --- loads and stores --------------------------------------------------------
w16f_m_fld32:
    mov cx, 4
    call w16f_rd
    mov di, w16f_a
    call w16f_up32
    jmp short w16f_pusha
w16f_m_fld64:
    mov cx, 8
    call w16f_rd
    mov di, w16f_a
    call w16f_up64
    jmp short w16f_pusha
w16f_m_fld80:
    mov cx, 10
    call w16f_rd
    mov si, w16f_mbuf               ; as it is: no rounding, no exception
    call w16f_rtag
    jmp w16f_pushraw
w16f_m_fild16:
    mov cx, 2
    jmp short w16f_fild
w16f_m_fild32:
    mov cx, 4
    jmp short w16f_fild
w16f_m_fild64:
    mov cx, 8
w16f_fild:
    call w16f_rd
    mov di, w16f_a
    call w16f_upint
w16f_pusha:
    mov si, w16f_a
    jmp w16f_push

w16f_m_fst32:
    xor dl, dl
    jmp short w16f_fst32
w16f_m_fstp32:
    mov dl, 1
w16f_fst32:
    push dx
    mov cl, 0
    mov di, w16f_a
    call w16f_ldst
    call w16f_r32
    call w16f_qnan
    mov di, w16f_a
    call w16f_rnd
    mov si, w16f_a
    call w16f_pk32
    mov cx, 4
    call w16f_wr
    pop dx
    or dl, dl
    jz .out
    jmp w16f_pop
.out:
    ret

w16f_m_fst64:
    xor dl, dl
    jmp short w16f_fst64
w16f_m_fstp64:
    mov dl, 1
w16f_fst64:
    push dx
    mov cl, 0
    mov di, w16f_a
    call w16f_ldst
    call w16f_r64
    call w16f_qnan
    mov di, w16f_a
    call w16f_rnd
    mov si, w16f_a
    call w16f_pk64
    mov cx, 8
    call w16f_wr
    pop dx
    or dl, dl
    jz .out
    jmp w16f_pop
.out:
    ret

; w16f_qnan - a NaN stored to memory: quiet, the invalid for a signalling one
w16f_qnan:
    cmp byte [w16f_a + W16FO_K], FK_NAN
    jne .out
    test byte [w16f_a + 9], 0x40
    jnz .out
    mov al, FE_IE
    call w16f_raise
    or byte [w16f_a + 9], 0x40
.out:
    ret

w16f_m_fstp80:
    mov cl, 0
    call w16f_needst
    jnc .full
    cmp byte [w16f_nowr], 0         ; empty: the indefinite, masked
    jne .out
    mov si, w16f_indef
    jmp short .copy
.full:
    mov cl, 0
    call w16f_phys
.copy:
    push es
    push ds
    pop es
    mov di, w16f_mbuf
    mov cx, 5
    rep movsw
    pop es
    mov cx, 10
    call w16f_wr
    jmp w16f_pop
.out:
    ret

; fist / fistp: st(0) to an integer in the rounding mode, the integer
; indefinite for a NaN, an infinity, or a value out of its range
w16f_m_fist16:
    mov cx, 2
    jmp short w16f_fist
w16f_m_fistp16:
    mov cx, 2
    jmp short w16f_fistp
w16f_m_fist32:
    mov cx, 4
    jmp short w16f_fist
w16f_m_fistp32:
    mov cx, 4
    jmp short w16f_fistp
w16f_m_fistp64:
    mov cx, 8
w16f_fistp:
    call w16f_fisto
    call w16f_wr
    jmp w16f_pop
w16f_fist:
    call w16f_fisto
    jmp w16f_wr

; w16f_fisto - st(0) as an integer of CX bytes into w16f_mbuf. Keeps CX
w16f_fisto:
    push cx
    push cx
    mov cl, 0
    mov di, w16f_a
    call w16f_ldst
    pop cx
    xor ax, ax                      ; the answer's eight bytes, zero
    mov [w16f_mbuf], ax
    mov [w16f_mbuf + 2], ax
    mov [w16f_mbuf + 4], ax
    mov [w16f_mbuf + 6], ax
    mov al, [w16f_a + W16FO_K]
    cmp al, FK_ZERO
    je .out
    cmp al, FK_NORM
    jne .bad
    mov di, w16f_a
    call w16f_toint                 ; the value rounded to an integer
    cmp byte [w16f_a + W16FO_K], FK_ZERO
    je .out
    mov ax, cx                      ; the bits, less the sign's
    shl ax, 1
    shl ax, 1
    shl ax, 1
    dec ax
    mov dx, [w16f_a + W16FO_E]
    cmp dx, ax
    jl .fits
    jg .bad
    cmp byte [w16f_a + W16FO_S], 0  ; exactly 2^(bits-1): only negative
    je .bad
    cmp word [w16f_a + 8], 0x8000
    jne .bad
    push di
    mov di, w16f_a
    mov word [di + 8], 0            ; (is it 1.000...?)
    call w16f_isz
    mov word [di + 8], 0x8000
    pop di
    jnz .bad
.fits:
    push cx                         ; the magnitude: down by 63 - e
    mov cx, 63
    sub cx, [w16f_a + W16FO_E]
    mov di, w16f_a
    call w16f_shr
    pop cx
    mov ax, [w16f_a + 2]
    mov [w16f_mbuf], ax
    mov ax, [w16f_a + 4]
    mov [w16f_mbuf + 2], ax
    mov ax, [w16f_a + 6]
    mov [w16f_mbuf + 4], ax
    mov ax, [w16f_a + 8]
    mov [w16f_mbuf + 6], ax
    cmp byte [w16f_a + W16FO_S], 0
    je .out
    not word [w16f_mbuf]
    not word [w16f_mbuf + 2]
    not word [w16f_mbuf + 4]
    not word [w16f_mbuf + 6]
    add word [w16f_mbuf], 1
    adc word [w16f_mbuf + 2], 0
    adc word [w16f_mbuf + 4], 0
    adc word [w16f_mbuf + 6], 0
    jmp short .out
.bad:
    and byte [w16f_exc], ~FE_PE     ; the invalid alone, as the 8087
    mov al, FE_IE
    call w16f_raise
    mov bx, cx                      ; the integer indefinite: the top bit
    mov byte [w16f_mbuf + bx - 1], 0x80
.out:
    pop cx
    ret

; w16f_toint - DI (normal) rounded to an integer in the rounding mode, PE
; when that lost anything. It stays a value
w16f_toint:
    push cx
    mov cx, [di + W16FO_E]
    cmp cx, 63
    jge .out                        ; an integer already
    or cx, cx
    jns .p
    neg cx                          ; below one: down to the units place
    cmp cx, 100
    jbe .s
    mov cx, 100
.s:
    call w16f_shr
    mov word [di + W16FO_E], 0
    xor cx, cx
.p:
    inc cx                          ; the bits of the integer part
    call w16f_rndp
    cmp byte [w16f_inx], 0
    je .z
    mov al, FE_PE
    call w16f_raise
.z:
    call w16f_isz
    jnz .out
    mov byte [di + W16FO_K], FK_ZERO
.out:
    pop cx
    ret

; --- the D9 register forms -----------------------------------------------------
w16f_r_fld:                         ; fld st(i): a copy keeps every bit
    call w16f_needst
    mov si, w16f_indef
    mov al, FT_SPEC
    jc w16f_pushraw
    call w16f_phys
    jmp w16f_pushraw

w16f_r_fxch:
    push cx
    call w16f_needst
    pop cx
    pushf
    push cx
    mov cl, 0
    call w16f_needst
    pop cx
    jc .e
    popf
    jc .e1
    jmp short .swap
.e:
    popf
.e1:
    cmp byte [w16f_nowr], 0
    jne .out
.swap:
    push cx                         ; the two registers' bytes and tags
    mov cl, 0
    call w16f_phys
    mov dx, bx
    mov di, si
    pop cx
    call w16f_phys
    push cx
    mov cx, 5
.x:
    mov ax, [si]
    xchg ax, [di]
    mov [si], ax
    inc si
    inc si
    inc di
    inc di
    loop .x
    pop cx
    mov al, [w16f_tag + bx]
    xchg bx, dx
    xchg al, [w16f_tag + bx]
    xchg bx, dx
    mov [w16f_tag + bx], al
    call .fill                      ; an empty one swapped in: indefinite
    push cx
    mov cl, 0
    call .fill
    pop cx
.out:
    ret
.fill:
    call w16f_phys
    cmp al, FT_EMPTY
    jne .fr
    push cx
    mov di, si
    mov si, w16f_indef
    push es
    push ds
    pop es
    mov cx, 5
    rep movsw
    pop es
    pop cx
    mov byte [w16f_tag + bx], FT_SPEC
.fr:
    ret

w16f_r_d9_2:                        ; D9 D0: fnop
    or cl, cl
    jnz .u
    ret
.u:
    jmp w16f_unsup

w16f_r_fstp1:                       ; D9 D8+i, DF D0+i, DF D8+i: fstp st(i)
    mov dl, 1
    jmp short w16f_fstreg
w16f_r_fst:                         ; DD D0+i
    xor dl, dl
    jmp short w16f_fstreg
w16f_r_fstp:                        ; DD D8+i
    mov dl, 1
w16f_fstreg:
    push cx
    mov cl, 0
    call w16f_needst
    pop cx
    jnc .ok
    cmp byte [w16f_nowr], 0
    jne .out
.ok:
    push cx
    mov cl, 0
    call w16f_phys
    mov dh, al                      ; (an empty top: the indefinite)
    cmp al, FT_EMPTY
    jne .src
    mov si, w16f_indef
    mov dh, FT_SPEC
.src:
    push si
    pop ax
    pop cx
    push ax
    call w16f_phys
    mov di, si
    pop si
    push cx
    push es
    push ds
    pop es
    mov cx, 5
    rep movsw
    pop es
    pop cx
    mov [w16f_tag + bx], dh
    or dl, dl
    jz .out
    jmp w16f_pop
.out:
    ret

w16f_r_d9_4:                        ; fchs fabs - - ftst fxam - -
    cmp cl, 0
    je .chs
    cmp cl, 1
    je .abs
    cmp cl, 4
    je .tst
    cmp cl, 5
    je .xam
    jmp w16f_unsup
.chs:
    mov ah, 0x80
    jmp short .sign
.abs:
    mov ah, 0
.sign:
    mov cl, 0
    call w16f_needst
    jc .se
    call w16f_phys
    cmp ah, 0
    je .clr
    xor byte [si + 9], 0x80
    ret
.clr:
    and byte [si + 9], 0x7F
    ret
.se:
    cmp byte [w16f_nowr], 0         ; empty: the indefinite, masked
    jne .r
    mov cl, 0
    call w16f_phys
    push cx
    mov di, si
    mov si, w16f_indef
    push es
    push ds
    pop es
    mov cx, 5
    rep movsw
    pop es
    pop cx
    mov byte [w16f_tag + bx], FT_SPEC
.r:
    ret
.tst:
    mov cl, 0
    mov di, w16f_a
    call w16f_ldst
    mov byte [w16f_b + W16FO_K], FK_ZERO
    mov byte [w16f_b + W16FO_S], 0
    call w16f_cmp
    jmp w16f_setcc
.xam:
    mov cl, 0                       ; C1 the sign, C3 C2 C0 the class
    call w16f_phys
    mov dl, [si + 9]
    and byte [w16f_sw + 1], 0xB8    ; C3 C2 C1 C0 cleared
    test dl, 0x80
    jz .xs
    or byte [w16f_sw + 1], 0x02
.xs:
    mov ah, 0x41                    ; empty: C3 C0
    cmp al, FT_EMPTY
    je .xset
    mov di, w16f_a
    push word [w16f_exc]
    call w16f_up80
    pop ax
    mov [w16f_exc], al              ; (fxam raises nothing)
    mov al, [w16f_a + W16FO_K]
    mov ah, 0x01                    ; NaN: C0
    cmp al, FK_NAN
    je .xset
    mov ah, 0x05                    ; infinity: C2 C0
    cmp al, FK_INF
    je .xset
    mov ah, 0x40                    ; zero: C3
    cmp al, FK_ZERO
    je .xset
    mov ah, 0x04                    ; normal: C2
    test byte [si + 7], 0x80
    jnz .xset
    mov ah, 0x44                    ; denormal: C3 C2
.xset:
    or [w16f_sw + 1], ah
    ret

w16f_r_d9_5:                        ; the constants
    cmp cl, 7
    jne .k
    jmp w16f_unsup
.k:
    xor ch, ch
    mov si, cx
    shl si, 1
    mov ax, si
    shl si, 1
    shl si, 1
    add si, ax                      ; CL * 10
    add si, w16f_consts
    mov di, w16f_a
    call w16f_up80
    mov si, w16f_a
    jmp w16f_push

w16f_consts:
    dw 0x0000, 0x0000, 0x0000, 0x8000, 0x3FFF   ; 1
    dw 0x8AFE, 0xCD1B, 0x784B, 0xD49A, 0x4000   ; log2 10
    dw 0xF0BC, 0x5C17, 0x3B29, 0xB8AA, 0x3FFF   ; log2 e
    dw 0xC235, 0x2168, 0xDAA2, 0xC90F, 0x4000   ; pi
    dw 0xF799, 0xFBCF, 0x9A84, 0x9A20, 0x3FFD   ; log10 2
    dw 0x79AC, 0xD1CF, 0x17F7, 0xB172, 0x3FFE   ; ln 2
    dw 0x0000, 0x0000, 0x0000, 0x0000, 0x0000   ; 0

w16f_r_d9_6:                        ; - - - - fxtract - fdecstp fincstp
    cmp cl, 4
    je .xtr
    cmp cl, 6
    je .dec
    cmp cl, 7
    je .inc
    jmp w16f_unsup
.dec:
    dec byte [w16f_tp]
    and byte [w16f_tp], 7
    ret
.inc:
    inc byte [w16f_tp]
    and byte [w16f_tp], 7
    ret
.xtr:
    mov cl, 0
    mov di, w16f_a
    call w16f_ldst
    mov al, [w16f_a + W16FO_K]
    cmp al, FK_NAN
    je .xn
    cmp al, FK_INF
    je .xi
    cmp al, FK_ZERO
    je .xz
    mov ax, [w16f_a + W16FO_E]      ; b = the exponent, as a value
    mov word [w16f_a + W16FO_E], 0
    mov [w16f_mbuf], ax
    mov cx, 2
    mov di, w16f_b
    call w16f_upint
    jmp short .xput
.xz:
    mov al, FE_ZE                   ; zero: the exponent -inf
    call w16f_raise
    mov byte [w16f_b + W16FO_K], FK_INF
    mov byte [w16f_b + W16FO_S], 0x80
    jmp short .xput
.xi:
    mov byte [w16f_b + W16FO_K], FK_INF     ; inf: the exponent +inf
    mov byte [w16f_b + W16FO_S], 0
    jmp short .xput
.xn:
    call w16f_qnan                  ; a NaN: both are it
    push es
    push ds
    pop es
    mov si, w16f_a
    mov di, w16f_b
    mov cx, W16FO_SIZE / 2
    rep movsw
    pop es
.xput:
    mov cl, 0                       ; st(0) := the exponent, push the
    mov si, w16f_b                  ;   significand
    call w16f_setst
    mov si, w16f_a
    jmp w16f_push

w16f_r_d9_7:                        ; fprem - fsqrt - frndint fscale - -
    cmp cl, 0
    je .prem
    cmp cl, 2
    je .sqrt
    cmp cl, 4
    je w16f_x_rndint
    cmp cl, 5
    je .scale
    jmp w16f_unsup
.sqrt:
    mov cl, 0
    mov di, w16f_a
    call w16f_ldst
    call w16f_sqrt
    mov cl, 0
    mov si, w16f_a
    jmp w16f_setst
.scale:
    mov cl, 1
    mov di, w16f_b
    call w16f_ldst
    mov cl, 0
    mov di, w16f_a
    call w16f_ldst
    call w16f_nan2
    jc .sput
    mov al, [w16f_b + W16FO_K]
    cmp al, FK_INF
    je .sinf
    cmp al, FK_ZERO
    je .sput
    cmp byte [w16f_a + W16FO_K], FK_NORM
    jne .sput
    mov di, w16f_b                  ; k = trunc(st(1)), held to +-32000
    mov ax, [di + W16FO_E]
    cmp ax, 15
    jl .sk
    mov dx, 32000
    jmp short .ssig
.sk:
    mov dx, 0
    or ax, ax
    js .ssig
    push cx
    mov cx, 15
    sub cx, ax
    mov dx, [di + 8]
    shr dx, cl
    pop cx
.ssig:
    cmp byte [di + W16FO_S], 0
    je .sadd
    neg dx
.sadd:
    mov ax, [w16f_a + W16FO_E]
    call w16f_eadd
    mov [w16f_a + W16FO_E], ax
    mov di, w16f_a
    call w16f_rx
    call w16f_rnd
    jmp short .sput
.sinf:
    mov al, [w16f_a + W16FO_K]      ; 2^+inf / 2^-inf
    cmp byte [w16f_b + W16FO_S], 0
    jne .sneg
    cmp al, FK_ZERO
    je .sbad
    mov byte [w16f_a + W16FO_K], FK_INF
    jmp short .sput
.sneg:
    cmp al, FK_INF
    je .sbad
    mov byte [w16f_a + W16FO_K], FK_ZERO
    jmp short .sput
.sbad:
    call w16f_indefa
.sput:
    mov cl, 0
    mov si, w16f_a
    jmp w16f_setst
.prem:
    jmp w16f_x_prem

; w16f_x_rndint - frndint (and __FPMATH 6)
w16f_x_rndint:
    mov cl, 0
    mov di, w16f_a
    call w16f_ldst
    mov al, [w16f_a + W16FO_K]
    cmp al, FK_NAN
    jne .nn
    call w16f_qnan
    jmp short .put
.nn:
    cmp al, FK_NORM
    jne .put
    mov di, w16f_a
    call w16f_toint
.put:
    mov cl, 0
    mov si, w16f_a
    jmp w16f_setst

; w16f_x_prem - fprem: st(0) := the remainder of st(0) / st(1), the quotient
; truncated, its low three bits in C0 C3 C1; C2 when it is only partial
w16f_x_prem:
    mov cl, 1
    mov di, w16f_b
    call w16f_ldst
    mov cl, 0
    mov di, w16f_a
    call w16f_ldst
    and byte [w16f_sw + 1], 0xB8    ; C3 C2 C1 C0
    call w16f_nan2
    jc .put
    mov al, [w16f_a + W16FO_K]
    mov ah, [w16f_b + W16FO_K]
    cmp al, FK_INF
    je .bad
    cmp ah, FK_ZERO
    je .bad
    cmp al, FK_ZERO
    je .put
    cmp ah, FK_INF
    je .put
    mov ax, [w16f_a + W16FO_E]
    sub ax, [w16f_b + W16FO_E]
    js .put                         ; |a| < |b|: a itself
    mov cx, ax                      ; CX = the steps; 64 or more: partial
    xor dl, dl
    cmp cx, 64
    jb .full
    mov cx, 63
    mov dl, 1
.full:
    mov ax, [w16f_a + 2]            ; R = a's 64 bits
    mov [w16f_rr], ax
    mov ax, [w16f_a + 4]
    mov [w16f_rr + 2], ax
    mov ax, [w16f_a + 6]
    mov [w16f_rr + 4], ax
    mov ax, [w16f_a + 8]
    mov [w16f_rr + 6], ax
    mov word [w16f_rr + 8], 0
    xor bx, bx                      ; BX = the quotient's low bits
    push cx
.step:
    shl bx, 1
    call w16f_cmprb
    jb .lo
    call w16f_subrb
    or bl, 1
.lo:
    jcxz .done
    call w16f_shlr
    dec cx
    jmp short .step
.done:
    pop cx
    mov ax, [w16f_rr]               ; the remainder, at a's exponent less
    mov [w16f_a + 2], ax            ;   the steps
    mov ax, [w16f_rr + 2]
    mov [w16f_a + 4], ax
    mov ax, [w16f_rr + 4]
    mov [w16f_a + 6], ax
    mov ax, [w16f_rr + 6]
    mov [w16f_a + 8], ax
    mov word [w16f_a], 0
    mov ax, [w16f_a + W16FO_E]
    sub ax, cx
    mov [w16f_a + W16FO_E], ax
    mov di, w16f_a
    call w16f_norm
    call w16f_rx
    call w16f_rnd
    mov ah, 0                       ; q2 -> C0, q1 -> C3, q0 -> C1; C2
    test bl, 4
    jz .q1
    or ah, 0x01
.q1:
    test bl, 2
    jz .q0
    or ah, 0x40
.q0:
    test bl, 1
    jz .c2
    or ah, 0x02
.c2:
    or dl, dl
    jz .cc
    or ah, 0x04
.cc:
    or [w16f_sw + 1], ah
    jmp short .put
.bad:
    call w16f_indefa
.put:
    mov cl, 0
    mov si, w16f_a
    jmp w16f_setst

; --- control ------------------------------------------------------------------
w16f_finit:
    mov word [w16f_cw], 0x037F
    mov word [w16f_sw], 0
    mov byte [w16f_tp], 0
    push di
    push cx
    push es
    push ds
    pop es
    mov di, w16f_tag
    mov al, FT_EMPTY
    mov cx, 8
    rep stosb
    pop es
    pop cx
    pop di
    ret

w16f_r_db_4:                        ; feni fdisi fclex finit fsetpm
    cmp cl, 2
    je .clex
    cmp cl, 3
    je w16f_finit
    cmp cl, 4
    ja .u
    ret
.u:
    jmp w16f_unsup
.clex:
    and word [w16f_sw], 0x7F00
    ret

w16f_r_ffree:                       ; DD C0+i
    call w16f_phys
    mov byte [w16f_tag + bx], FT_EMPTY
    ret
w16f_r_ffreep:                      ; DF C0+i
    call w16f_r_ffree
    jmp w16f_pop

w16f_r_df_4:                        ; DF E0: fstsw ax
    or cl, cl
    jz .ax
    jmp w16f_unsup
.ax:
    call w16f_swout
    mov [bp + W16FF_AX], ax
    ret

; w16f_swout - AX = the status word with TOP in it
w16f_swout:
    push cx
    mov ax, [w16f_sw]
    and ah, 0xC7
    mov cl, [w16f_tp]
    shl cl, 1
    shl cl, 1
    shl cl, 1
    or ah, cl
    pop cx
    ret

; w16f_twout - AX = the tag word, two bits a physical register
w16f_twout:
    push bx
    push cx
    xor ax, ax
    mov bx, 7
.t:
    shl ax, 1
    shl ax, 1
    or al, [w16f_tag + bx]
    dec bx
    jns .t
    pop cx
    pop bx
    ret

w16f_m_fldcw:
    mov cx, 2
    call w16f_rd
    mov ax, [w16f_mbuf]
    mov [w16f_cw], ax
    ret
w16f_m_fstcw:
    mov ax, [w16f_cw]
    mov [w16f_mbuf], ax
    mov cx, 2
    jmp w16f_wr
w16f_m_fstsw:
    call w16f_swout
    mov [w16f_mbuf], ax
    mov cx, 2
    jmp w16f_wr

; w16f_env - the 14-byte real-mode environment into w16f_mbuf: CW, SW, TW,
; and no instruction or operand pointer
w16f_env:
    push di
    push cx
    mov ax, [w16f_cw]
    mov [w16f_mbuf], ax
    call w16f_swout
    mov [w16f_mbuf + 2], ax
    call w16f_twout
    mov [w16f_mbuf + 4], ax
    mov di, w16f_mbuf + 6
    xor ax, ax
    mov cx, 4
.z:
    mov [di], ax
    inc di
    inc di
    loop .z
    pop cx
    pop di
    ret

; w16f_envin - w16f_mbuf's environment back
w16f_envin:
    push bx
    push cx
    mov ax, [w16f_mbuf]
    mov [w16f_cw], ax
    mov ax, [w16f_mbuf + 2]
    mov cl, ah
    shr cl, 1
    shr cl, 1
    shr cl, 1
    and cl, 7
    mov [w16f_tp], cl
    and ah, 0xC7
    mov [w16f_sw], ax
    mov ax, [w16f_mbuf + 4]
    xor bx, bx
.t:
    mov cl, al
    and cl, 3
    mov [w16f_tag + bx], cl
    shr ax, 1
    shr ax, 1
    inc bx
    cmp bx, 8
    jb .t
    pop cx
    pop bx
    ret

w16f_m_fstenv:                      ; then every exception masked, as Intel
    call w16f_env
    mov cx, 14
    call w16f_wr
    or byte [w16f_cw], 0x3F
    ret
w16f_m_fldenv:
    mov cx, 14
    call w16f_rd
    jmp w16f_envin
w16f_m_fsave:                       ; the environment, st(0)..st(7), finit
    call w16f_env
    mov di, w16f_mbuf + 14
    mov cl, 0
.r:
    push cx
    call w16f_phys
    push es
    push ds
    pop es
    mov cx, 5
    rep movsw
    pop es
    pop cx
    inc cl
    cmp cl, 8
    jb .r
    mov cx, 94
    call w16f_wr
    jmp w16f_finit
w16f_m_frstor:
    mov cx, 94
    call w16f_rd
    call w16f_envin
    mov si, w16f_mbuf + 14
    mov cl, 0
.r:
    push cx
    push si
    call w16f_phys
    mov di, si
    pop si
    push es
    push ds
    pop es
    mov cx, 5
    rep movsw
    pop es
    pop cx
    inc cl
    cmp cl, 8
    jb .r
    ret

w16f_unsup_t:
    jmp w16f_unsup

; --- the dispatch tables: escape * 8 + reg --------------------------------------
w16f_mtab:
    dw w16f_m_r32, w16f_m_r32, w16f_m_r32, w16f_m_r32           ; D8
    dw w16f_m_r32, w16f_m_r32, w16f_m_r32, w16f_m_r32
    dw w16f_m_fld32, w16f_unsup_t, w16f_m_fst32, w16f_m_fstp32  ; D9
    dw w16f_m_fldenv, w16f_m_fldcw, w16f_m_fstenv, w16f_m_fstcw
    dw w16f_m_i32, w16f_m_i32, w16f_m_i32, w16f_m_i32           ; DA
    dw w16f_m_i32, w16f_m_i32, w16f_m_i32, w16f_m_i32
    dw w16f_m_fild32, w16f_unsup_t, w16f_m_fist32, w16f_m_fistp32 ; DB
    dw w16f_unsup_t, w16f_m_fld80, w16f_unsup_t, w16f_m_fstp80
    dw w16f_m_r64, w16f_m_r64, w16f_m_r64, w16f_m_r64           ; DC
    dw w16f_m_r64, w16f_m_r64, w16f_m_r64, w16f_m_r64
    dw w16f_m_fld64, w16f_unsup_t, w16f_m_fst64, w16f_m_fstp64  ; DD
    dw w16f_m_frstor, w16f_unsup_t, w16f_m_fsave, w16f_m_fstsw
    dw w16f_m_i16, w16f_m_i16, w16f_m_i16, w16f_m_i16           ; DE
    dw w16f_m_i16, w16f_m_i16, w16f_m_i16, w16f_m_i16
    dw w16f_m_fild16, w16f_unsup_t, w16f_m_fist16, w16f_m_fistp16 ; DF
    dw w16f_unsup_t, w16f_m_fild64, w16f_unsup_t, w16f_m_fistp64

w16f_rtab:
    dw w16f_r_d8, w16f_r_d8, w16f_r_d8, w16f_r_d8               ; D8
    dw w16f_r_d8, w16f_r_d8, w16f_r_d8, w16f_r_d8
    dw w16f_r_fld, w16f_r_fxch, w16f_r_d9_2, w16f_r_fstp1       ; D9
    dw w16f_r_d9_4, w16f_r_d9_5, w16f_r_d9_6, w16f_r_d9_7
    dw w16f_unsup_t, w16f_unsup_t, w16f_unsup_t, w16f_unsup_t   ; DA
    dw w16f_unsup_t, w16f_unsup_t, w16f_unsup_t, w16f_unsup_t
    dw w16f_unsup_t, w16f_unsup_t, w16f_unsup_t, w16f_unsup_t   ; DB
    dw w16f_r_db_4, w16f_unsup_t, w16f_unsup_t, w16f_unsup_t
    dw w16f_r_dc, w16f_r_dc, w16f_r_dc, w16f_r_dc               ; DC
    dw w16f_r_dc, w16f_r_dc, w16f_r_dc, w16f_r_dc
    dw w16f_r_ffree, w16f_r_fxch, w16f_r_fst, w16f_r_fstp       ; DD
    dw w16f_unsup_t, w16f_unsup_t, w16f_unsup_t, w16f_unsup_t
    dw w16f_r_de, w16f_r_de, w16f_r_de, w16f_r_de               ; DE
    dw w16f_r_de, w16f_r_de, w16f_r_de, w16f_r_de
    dw w16f_r_ffreep, w16f_r_fxch, w16f_r_fstp1, w16f_r_fstp1   ; DF
    dw w16f_r_df_4, w16f_unsup_t, w16f_unsup_t, w16f_unsup_t

w16f_end:

section .text
