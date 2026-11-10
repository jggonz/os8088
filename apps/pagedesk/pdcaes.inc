; =============================================================================
; os8088 - apps/pagedesk/pdcaes.inc                AES-128/256, 8086 (PAGEDESK-SPEC 20.22.5)
;
; _pdc_aes_ks(rk, key, nk)              nk = 4 or 8; fills (nk+7)*16 bytes
; _pdc_aes_enc(rk, nr, in, out)         one block; in and out may be the same
; _pdc_aes_dec(rk, nr, in, out)
; _pdc_cbc_enc(rk, nr, iv, buf, nblk)   in place; iv is left = the last ciphertext block
; _pdc_cbc_dec(rk, nr, iv, buf, nblk)
; nr = nk + 6. SmallerC cdecl, near pointers into DS; AX BX CX DX lost, BP SI DI
; DS ES SS:SP DF kept. PDC_NOENC (PDENG) leaves the encrypting half out. The C twin is pdctwin.c. The state lives in four bytes
; of bss (aes_b, aes_t, aes_c); the S-box and its inverse are the only tables
; (512 bytes); InvMixColumns is MixColumns after the {04}/{05} pre-step.
; =============================================================================
%ifndef PDCAES_INC
%define PDCAES_INC 1
cpu 8086

section .bss
aes_b:   resb 16
aes_t:   resb 16
aes_c:   resb 16
aes_rkp: resw 1
aes_nr:  resw 1
aes_r:   resw 1
aes_nk:  resw 1
aes_k:   resw 1
aes_i:   resw 1
aes_tot: resw 1
aes_rcon: resb 1
section .text

%macro AES_XT 0                     ; AL = xtime(AL); AH lost
    shl al, 1
    sbb ah, ah
    and ah, 0x1b
    xor al, ah
%endmacro

aes_cp16:                           ; SI -> DI, 16 bytes; AX CX lost
    mov cx, 8
.l: mov ax, [si]
    mov [di], ax
    inc si
    inc si
    inc di
    inc di
    loop .l
    ret

aes_ark:                            ; aes_b ^= the 16 bytes at BX; AX CX SI lost
    mov si, aes_b
    mov cx, 8
.l: mov ax, [si]
    xor ax, [bx]
    mov [si], ax
    inc si
    inc si
    inc bx
    inc bx
    loop .l
    ret

; aes_perm: aes_b = sub[ aes_b[ perm[k] ] ]; SI = perm table, DX = sub table
aes_perm:
    mov di, aes_t
    mov bx, aes_b
    mov cx, 16
.l: lodsb
    xlat                            ; AL = aes_b[idx]
    xchg bx, dx
    xlat                            ; AL = sub[AL]
    xchg bx, dx
    mov [di], al
    inc di
    loop .l
    mov si, aes_t
    mov di, aes_b
    jmp aes_cp16

aes_mix:                            ; MixColumns in place on aes_b
    mov si, aes_b
    mov di, 4
.c: mov bl, [si]
    mov bh, [si+1]
    mov cl, [si+2]
    mov ch, [si+3]
    mov dl, bl
    xor dl, bh
    xor dl, cl
    xor dl, ch                      ; t
    mov al, bl
    xor al, bh
    AES_XT
    xor al, dl
    xor al, bl
    mov [si], al
    mov al, bh
    xor al, cl
    AES_XT
    xor al, dl
    xor al, bh
    mov [si+1], al
    mov al, cl
    xor al, ch
    AES_XT
    xor al, dl
    xor al, cl
    mov [si+2], al
    mov al, ch
    xor al, bl
    AES_XT
    xor al, dl
    xor al, ch
    mov [si+3], al
    add si, 4
    dec di
    jnz .c
    ret

aes_imix:                           ; the pre-step of InvMixColumns, in place on aes_b
    mov si, aes_b
    mov di, 4
.c: mov bl, [si]
    mov bh, [si+1]
    mov cl, [si+2]
    mov ch, [si+3]
    mov al, bl
    xor al, cl
    AES_XT
    AES_XT
    xor bl, al
    xor cl, al
    mov al, bh
    xor al, ch
    AES_XT
    AES_XT
    xor bh, al
    xor ch, al
    mov [si], bl
    mov [si+1], bh
    mov [si+2], cl
    mov [si+3], ch
    add si, 4
    dec di
    jnz .c
    ret

%ifndef PDC_NOENC
aes_encb:                           ; aes_b = E(aes_b)
    mov bx, [aes_rkp]
    call aes_ark
    mov word [aes_r], 1
.r: mov si, aes_shift
    mov dx, aes_sbox
    call aes_perm
    mov ax, [aes_r]
    cmp ax, [aes_nr]
    je .nm
    call aes_mix
.nm:
    mov ax, [aes_r]
    mov cl, 4
    shl ax, cl
    mov bx, [aes_rkp]
    add bx, ax
    call aes_ark
    inc word [aes_r]
    mov ax, [aes_r]
    cmp ax, [aes_nr]
    jbe .r
    ret

%endif
aes_decb:                           ; aes_b = D(aes_b)
    mov ax, [aes_nr]
    mov [aes_r], ax
    mov cl, 4
    shl ax, cl
    mov bx, [aes_rkp]
    add bx, ax
    call aes_ark
.r: dec word [aes_r]
    mov si, aes_ishift
    mov dx, aes_isbox
    call aes_perm
    mov ax, [aes_r]
    mov cl, 4
    shl ax, cl
    mov bx, [aes_rkp]
    add bx, ax
    call aes_ark
    cmp word [aes_r], 0
    je .done
    call aes_imix
    call aes_mix
    jmp .r
.done:
    ret

%ifndef PDC_NOENC
_pdc_aes_enc:
    push bp
    mov bp, sp
    push si
    push di
    mov ax, [bp+4]
    mov [aes_rkp], ax
    mov ax, [bp+6]
    mov [aes_nr], ax
    mov si, [bp+8]
    mov di, aes_b
    call aes_cp16
    call aes_encb
    mov si, aes_b
    mov di, [bp+10]
    call aes_cp16
    pop di
    pop si
    pop bp
    ret

%endif
_pdc_aes_dec:
    push bp
    mov bp, sp
    push si
    push di
    mov ax, [bp+4]
    mov [aes_rkp], ax
    mov ax, [bp+6]
    mov [aes_nr], ax
    mov si, [bp+8]
    mov di, aes_b
    call aes_cp16
    call aes_decb
    mov si, aes_b
    mov di, [bp+10]
    call aes_cp16
    pop di
    pop si
    pop bp
    ret

%ifndef PDC_NOENC
; _pdc_cbc_enc(rk, nr, iv, buf, nblk)
_pdc_cbc_enc:
    push bp
    mov bp, sp
    push si
    push di
    mov ax, [bp+4]
    mov [aes_rkp], ax
    mov ax, [bp+6]
    mov [aes_nr], ax
.blk:
    cmp word [bp+12], 0
    je .done
    mov si, [bp+10]                 ; buf
    mov bx, [bp+8]                  ; iv
    mov di, aes_b
    mov cx, 8
.x: mov ax, [si]
    xor ax, [bx]
    mov [di], ax
    inc si
    inc si
    inc bx
    inc bx
    inc di
    inc di
    loop .x
    call aes_encb
    mov si, aes_b
    mov di, [bp+10]
    call aes_cp16
    mov si, aes_b
    mov di, [bp+8]
    call aes_cp16
    add word [bp+10], 16
    dec word [bp+12]
    jmp .blk
.done:
    pop di
    pop si
    pop bp
    ret

%endif
_pdc_cbc_dec:
    push bp
    mov bp, sp
    push si
    push di
    mov ax, [bp+4]
    mov [aes_rkp], ax
    mov ax, [bp+6]
    mov [aes_nr], ax
.blk:
    cmp word [bp+12], 0
    je .done
    mov si, [bp+10]
    mov di, aes_c
    call aes_cp16                   ; the ciphertext, kept for the chain
    mov si, aes_c
    mov di, aes_b
    call aes_cp16
    call aes_decb
    mov si, aes_b
    mov bx, [bp+8]
    mov di, [bp+10]
    mov cx, 8
.x: mov ax, [si]
    xor ax, [bx]
    mov [di], ax
    inc si
    inc si
    inc bx
    inc bx
    inc di
    inc di
    loop .x
    mov si, aes_c
    mov di, [bp+8]
    call aes_cp16
    add word [bp+10], 16
    dec word [bp+12]
    jmp .blk
.done:
    pop di
    pop si
    pop bp
    ret

; _pdc_aes_ks(rk, key, nk)
%macro AES_SUB4 0                   ; DL DH CL CH through the S-box; BX lost
    mov bx, aes_sbox
    mov al, dl
    xlat
    mov dl, al
    mov al, dh
    xlat
    mov dh, al
    mov al, cl
    xlat
    mov cl, al
    mov al, ch
    xlat
    mov ch, al
%endmacro

_pdc_aes_ks:
    push bp
    mov bp, sp
    push si
    push di
    mov di, [bp+4]                  ; rk
    mov si, [bp+6]                  ; key
    mov ax, [bp+8]
    mov [aes_nk], ax
    mov cx, ax
    shl cx, 1
    shl cx, 1
.cp:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    loop .cp                        ; DI = word nk
    mov si, [bp+4]                  ; SI = word i - nk
    mov ax, [aes_nk]
    add ax, 7
    shl ax, 1
    shl ax, 1
    mov [aes_tot], ax
    mov ax, [aes_nk]
    mov [aes_i], ax
    mov word [aes_k], 0
    mov byte [aes_rcon], 1
.w: mov dl, [di-4]
    mov dh, [di-3]
    mov cl, [di-2]
    mov ch, [di-1]
    mov ax, [aes_k]
    or ax, ax
    jnz .nz
    mov al, dl                      ; RotWord
    mov dl, dh
    mov dh, cl
    mov cl, ch
    mov ch, al
    AES_SUB4
    xor dl, [aes_rcon]
    mov al, [aes_rcon]
    AES_XT
    mov [aes_rcon], al
    jmp short .x
.nz:
    cmp word [aes_nk], 8
    jne .x
    cmp ax, 4
    jne .x
    AES_SUB4
.x: mov al, dl
    xor al, [si]
    mov [di], al
    mov al, dh
    xor al, [si+1]
    mov [di+1], al
    mov al, cl
    xor al, [si+2]
    mov [di+2], al
    mov al, ch
    xor al, [si+3]
    mov [di+3], al
    add si, 4
    add di, 4
    inc word [aes_k]
    mov ax, [aes_k]
    cmp ax, [aes_nk]
    jb .nw
    mov word [aes_k], 0
.nw:
    inc word [aes_i]
    mov ax, [aes_i]
    cmp ax, [aes_tot]
    jae .end
    jmp .w
.end:
    pop di
    pop si
    pop bp
    ret

aes_shift:  db 0, 5, 10, 15, 4, 9, 14, 3, 8, 13, 2, 7, 12, 1, 6, 11
aes_ishift: db 0, 13, 10, 7, 4, 1, 14, 11, 8, 5, 2, 15, 12, 9, 6, 3
aes_sbox:
        db 0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76
        db 0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0
        db 0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15
        db 0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75
        db 0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84
        db 0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf
        db 0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8
        db 0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2
        db 0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73
        db 0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb
        db 0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79
        db 0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08
        db 0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a
        db 0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e
        db 0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf
        db 0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
aes_isbox:
        db 0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb
        db 0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb
        db 0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e
        db 0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25
        db 0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92
        db 0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84
        db 0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06
        db 0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b
        db 0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73
        db 0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e
        db 0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89, 0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b
        db 0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4
        db 0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f
        db 0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef
        db 0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61
        db 0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d
%endif
