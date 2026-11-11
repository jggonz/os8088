; =============================================================================
; os8088 - apps/pagedesk/pdcaes.inc                AES-128/256, 8086 (PAGEDESK-SPEC 20.22.5)
;
; _pdc_aes_ks(rk, key, nk)              nk = 4 or 8; fills (nk+7)*16 bytes
; _pdc_aes_enc(rk, nr, in, out)         one block; in and out may be the same
; _pdc_aes_dec(rk, nr, in, out)
; _pdc_cbc_enc(rk, nr, iv, buf, nblk)   in place; iv is left = the last ciphertext block
; _pdc_cbc_dec(rk, nr, iv, buf, nblk)
; _pdc_aes_dks(rk, nr)                  rk from _pdc_aes_ks -> the DECRYPTION schedule, in place (the equivalent
;                                       inverse cipher, FIPS 197 5.3.5). _pdc_aes_dec and _pdc_cbc_dec need
;                                       it; call it once per schedule, and never encrypt with the result
; nr = nk + 6. SmallerC cdecl, near pointers into DS; AX BX CX DX lost, BP SI DI
; DS ES SS:SP DF kept. PDC_NOENC (PDENG) leaves the encrypting half out. The C twin is pdctwin.c. The state lives in four bytes
; of bss (aes_b, aes_t, aes_c); the S-box and its inverse are the only tables
; (512 bytes) and, for decryption, Td0 (1 KiB; PDC_TD1 adds Td1, another 1 KiB and about 3.5 -> 4.1 KB/s) in bss (aes_td, filled on first use).
; =============================================================================
%ifndef PDCAES_INC
%define PDCAES_INC 1
cpu 8086

%ifdef PDC_FARST                    ; PDENG: the schedule, Td0 and the RC4 states live in a far area (the arena), ES = _pdc_cseg
%ifndef PDC_NOENC
%error "PDC_FARST is the decrypting half only"
%endif
%define RKP es:
%define RKB es:
%else
%define RKP
%define RKB ds:
%endif
section .bss
%ifdef PDC_FARST
%ifndef PDC_CSEG_DEF
%define PDC_CSEG_DEF 1
_pdc_cseg: resw 1
%endif
%endif
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
aes_tdok: resb 1
%ifdef PDC_FARST
aes_td   equ 0                      ; Td0 at offset 0 of the far area; _pdc_aes_tdnew rebuilds it on the next dks
%else
%ifdef PDC_TD1
aes_td:  resb 2048                  ; Td0 and Td1 as word planes, built on first use: low words +2x, high words +512+2x, then +1024 for Td1
%else
aes_td:  resb 1024                  ; Td0 as two word planes, built on first use: low words +2x, high words +512+2x
%endif
%endif
section .text

%macro AES_XT 0                     ; AL = xtime(AL); AH lost
    shl al, 1
    sbb ah, ah
    and ah, 0x1b
    xor al, ah
%endmacro

%ifdef PDC_FARST
%macro AES_ES 0                     ; ES = the cipher area (the caller pushes ES: stkbalance reads no macro)
    mov es, [_pdc_cseg]
%endmacro
aes_cp16:                           ; SI -> DI, 16 bytes in DS; CX lost
    push es
    push ds
    pop es
    mov cx, 8
    rep movsw
    pop es
    ret
_pdc_aes_tdnew:
    mov byte [aes_tdok], 0
    ret
%else
%macro AES_ES 0                     ; the string moves below copy DS to DS: ES is borrowed (the caller pushes and restores it)
    push ds
    pop es
%endmacro
aes_cp16:                           ; SI -> DI, 16 bytes (ES = DS, DF clear); CX lost
    mov cx, 8
    rep movsw
    ret
_pdc_aes_tdnew:
    mov byte [aes_tdok], 0
    ret
%endif

aes_ark:                            ; aes_b ^= the 16 bytes at BX; AX CX SI lost
    mov si, aes_b
    mov cx, 8
.l: mov ax, [si]
    xor ax, [RKP bx]
    mov [si], ax
    inc si
    inc si
    inc bx
    inc bx
    loop .l
    ret

%ifndef PDC_NOENC
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
; ---- decryption: the equivalent inverse cipher over two 1 KiB tables ----------------------------------
; Td0[x] = (14 s, 9 s, 13 s, 11 s) for s = InvSbox[x], bytes b0..b3 = rows 0..3, and Td1[x] = (b3, b0, b1, b2).
; Td2 and Td3 are those with their words swapped, so they are read from the same planes the other way round.
; A round is 16 lookups; the key added is the schedule _pdc_aes_dks made.
%define AES_T0L aes_td
%define AES_T0H aes_td + 512
%define AES_T1L aes_td + 1024
%define AES_T1H aes_td + 1536

%macro TDX 2                        ; ax:dx ^= Td<%1>[x] (mov, not xor, when %2 = 1); BX = 2x
 %if %2 = 1
  %define TDOP mov
 %else
  %define TDOP xor
 %endif
 %if %1 = 0
    TDOP ax, [RKP bx + AES_T0L]
    TDOP dx, [RKP bx + AES_T0H]
 %elif %1 = 2
    TDOP ax, [RKP bx + AES_T0H]
    TDOP dx, [RKP bx + AES_T0L]
 %elifdef PDC_TD1
  %if %1 = 1
    TDOP ax, [RKP bx + AES_T1L]
    TDOP dx, [RKP bx + AES_T1H]
  %else
    TDOP ax, [RKP bx + AES_T1H]
    TDOP dx, [RKP bx + AES_T1L]
  %endif
 %elif %1 = 1                       ; Td1 from Td0's bytes: (b3, b0, b1, b2)
    xor al, [RKP bx + AES_T0H + 1]
    xor ah, [RKP bx + AES_T0L]
    xor dl, [RKP bx + AES_T0L + 1]
    xor dh, [RKP bx + AES_T0H]
 %else                              ; Td3 = (b1, b2, b3, b0)
    xor al, [RKP bx + AES_T0L + 1]
    xor ah, [RKP bx + AES_T0H]
    xor dl, [RKP bx + AES_T0H + 1]
    xor dh, [RKP bx + AES_T0L]
 %endif
 %undef TDOP
%endmacro

%macro TDIDX 3                      ; BX = 2 * byte [si + %1]; ax:dx ^= Td<%2>[it] (a mov when %3 = 1)
    mov bl, [si + %1]
    xor bh, bh
    shl bx, 1
    TDX %2, %3
%endmacro

%macro TDCOL 5                      ; column %1 (its four source offsets %2..%5) -> [di + 4 * %1], key at [bp + 4 * %1]
    TDIDX %2, 0, 1
    TDIDX %3, 1, 0
    TDIDX %4, 2, 0
    TDIDX %5, 3, 0
    xor ax, [RKB bp + 4 * %1]
    xor dx, [RKB bp + 4 * %1 + 2]
    mov [di + 4 * %1], ax
    mov [di + 4 * %1 + 2], dx
%endmacro

aes_tdinit:                         ; fill the table once; AX BX CX DX SI DI lost
    cmp byte [aes_tdok], 0
    jne .done
    mov byte [aes_tdok], 1
    xor bx, bx
.x: mov al, [bx + aes_isbox]
    mov dh, al                      ; s
    AES_XT
    mov cl, al                      ; 2s
    AES_XT
    mov ch, al                      ; 4s
    AES_XT
    mov dl, al                      ; 8s
    mov al, dl
    xor al, ch
    xor al, cl                      ; 14 s
    mov ah, dl
    xor ah, dh                      ; 9 s
    mov di, bx
    shl di, 1
    mov [RKP di + AES_T0L], ax
    mov al, dl
    xor al, ch
    xor al, dh                      ; 13 s
    mov ah, dl
    xor ah, cl
    xor ah, dh                      ; 11 s
    mov [RKP di + AES_T0H], ax
%ifdef PDC_TD1
    mov al, [RKP di + AES_T0H + 1]      ; Td1 = (b3, b0, b1, b2)
    mov ah, [RKP di + AES_T0L]
    mov [RKP di + AES_T1L], ax
    mov al, [RKP di + AES_T0L + 1]
    mov ah, [RKP di + AES_T0H]
    mov [RKP di + AES_T1H], ax
%endif
    inc bx
    cmp bx, 256
    jb .x
.done:
    ret

aes_decb:                           ; aes_b = D(aes_b), with the decryption schedule
    push bp
    mov ax, [aes_nr]
    mov [aes_r], ax
    mov cl, 4
    shl ax, cl
    mov bx, [aes_rkp]
    add bx, ax
    mov bp, bx
    call aes_ark
    sub bp, 16                      ; round nr-1's key
    mov si, aes_b
    mov di, aes_t
    dec word [aes_r]                ; nr-1 rounds
.r: TDCOL 0, 0, 13, 10, 7
    TDCOL 1, 4, 1, 14, 11
    TDCOL 2, 8, 5, 2, 15
    TDCOL 3, 12, 9, 6, 3
    sub bp, 16
    xchg si, di
    dec word [aes_r]
    jz .fin
    jmp .r
.fin:                               ; SI = where the last round wrote: InvShiftRows, InvSubBytes, rk[0]
    mov bx, si
    mov si, aes_ishift
    mov dx, aes_isbox
    mov di, aes_b
    mov cx, 16
.l: lodsb
    xlat
    xchg bx, dx
    xlat
    xchg bx, dx
    mov [di], al                    ; (ES is the kernel's: no stosb)
    inc di
    loop .l
    mov bx, [aes_rkp]
    pop bp
    jmp aes_ark

; _pdc_aes_dks(rk, nr)
_pdc_aes_dks:
    push bp
    mov bp, sp
    push si
    push di
    push es
    AES_ES
    call aes_tdinit
    mov si, [bp+4]
    add si, 16
    mov cx, [bp+6]
    dec cx
    shl cx, 1
    shl cx, 1                       ; 4 * (nr-1) words
.w: xor ax, ax
    xor dx, dx
    mov bl, [RKP si]
    xor bh, bh
    mov bl, [bx + aes_sbox]
    shl bx, 1
    TDX 0, 0
    mov bl, [RKP si+1]
    xor bh, bh
    mov bl, [bx + aes_sbox]
    shl bx, 1
    TDX 1, 0
    mov bl, [RKP si+2]
    xor bh, bh
    mov bl, [bx + aes_sbox]
    shl bx, 1
    TDX 2, 0
    mov bl, [RKP si+3]
    xor bh, bh
    mov bl, [bx + aes_sbox]
    shl bx, 1
    TDX 3, 0
    mov [RKP si], ax
    mov [RKP si+2], dx
    add si, 4
    loop .w
    pop es
    pop di
    pop si
    pop bp
    ret

%ifndef PDC_NOENC
_pdc_aes_enc:
    push bp
    mov bp, sp
    push si
    push di
    push es
    AES_ES
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
    pop es
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
    push es
    AES_ES
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
    pop es
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
    push es
    AES_ES
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
    pop es
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
    push es
    AES_ES
    mov ax, [bp+4]
    mov [aes_rkp], ax
    mov ax, [bp+6]
    mov [aes_nr], ax
    mov cx, [bp+12]                 ; nblk
    or cx, cx
    jz .done
    mov ax, cx                      ; the last ciphertext block becomes the next iv
    mov cl, 4
    shl ax, cl
    add ax, [bp+10]
    sub ax, 16
    mov si, ax
    mov di, aes_c
    call aes_cp16
    ; the blocks go from the LAST to the first: P(i) = D(C(i)) ^ C(i-1), and C(i-1) is still in the buffer
.blk:
    mov ax, [bp+12]
    dec ax
    mov cl, 4
    shl ax, cl
    add ax, [bp+10]                 ; this block
    mov si, ax
    mov di, aes_b
    call aes_cp16
    call aes_decb
    mov ax, [bp+12]
    dec ax
    mov cl, 4
    shl ax, cl
    add ax, [bp+10]
    mov di, ax
    mov bx, [bp+8]                  ; the previous ciphertext: the iv for block 0
    cmp word [bp+12], 1
    je .x
    lea bx, [di-16]
.x: mov si, aes_b
    mov cx, 8
.xl:
    lodsw
    xor ax, [bx]
    inc bx
    inc bx
    mov [di], ax
    inc di
    inc di
    loop .xl
    dec word [bp+12]
    jnz .blk
    mov si, aes_c
    mov di, [bp+8]
    call aes_cp16                   ; iv = the last ciphertext block
.done:
    pop es
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
    push es
    AES_ES
    mov di, [bp+4]                  ; rk
    mov si, [bp+6]                  ; key
    mov ax, [bp+8]
    mov [aes_nk], ax
    mov cx, ax
    shl cx, 1
    shl cx, 1
.cp:
    mov al, [si]
    mov [RKP di], al
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
.w: mov dl, [RKP di-4]
    mov dh, [RKP di-3]
    mov cl, [RKP di-2]
    mov ch, [RKP di-1]
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
    xor al, [RKP si]
    mov [RKP di], al
    mov al, dh
    xor al, [RKP si+1]
    mov [RKP di+1], al
    mov al, cl
    xor al, [RKP si+2]
    mov [RKP di+2], al
    mov al, ch
    xor al, [RKP si+3]
    mov [RKP di+3], al
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
    pop es
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
