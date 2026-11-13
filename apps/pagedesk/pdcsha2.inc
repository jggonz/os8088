; =============================================================================
; os8088 - apps/pagedesk/pdcsha2.inc            SHA-256 / SHA-512, 8086 (PAGEDESK-SPEC 20.22.5)
;
; _pdc_sha256_block(st, blk)   64 bytes into st (pdw[16], eight words lo,hi)
; _pdc_sha512_block(st, blk)   128 bytes into st (pdw[32], eight words of four pdw)
; (the init functions are in C: pdxk.c. SHA-384 is the 512 block with other IVs.)
; SmallerC cdecl, near pointers into DS; AX BX CX DX lost, BP SI DI DS ES SS:SP DF
; kept. The rotations are macros, so each amount is a few byte moves and at
; most four single-bit steps (256) or a pair of CL shifts per word (512).
; C twin: pdctwin.c; tests/pdcrypt/pdc_diff.py holds the two to FIPS 180-4.
; =============================================================================
%ifndef PDCSHA2_INC
%define PDCSHA2_INC 1
cpu 8086

section .bss
s2w:    resw 128                    ; W[0..63] of SHA-256, or the first 64 words of 512's
s2v:    resw 16                     ; a..h of SHA-256
s2sg:   resw 2
s2sh:   resw 2
s2t1:   resw 2
s2st:   resw 1
s2blk:  resw 1
s5w:    resw 320                    ; W[0..79] of SHA-512 (4 words each)
s5v:    resw 32                     ; a..h
s5t1:   resw 4
s5t2:   resw 4
s5sg:   resw 4
s5sh:   resw 4
section .text

; ---- 32-bit rotates of BX:CX (BX high), DL scratch ----------------------------
%macro ROR1_32 0
    shr bx, 1
    rcr cx, 1
    jnc %%z
    or bx, 0x8000
%%z:
%endmacro
%macro ROL1_32 0
    shl cx, 1
    rcl bx, 1
    adc cx, 0
%endmacro
%macro ROR8_32 0
    mov dl, cl
    mov cl, ch
    mov ch, bl
    mov bl, bh
    mov bh, dl
%endmacro
%macro ROL8_32 0
    mov dl, bh
    mov bh, bl
    mov bl, ch
    mov ch, cl
    mov cl, dl
%endmacro
%macro ROTR32 1                     ; rotate BX:CX right by %1 (1..31)
  %assign %%n %1
  %if %%n >= 16
    xchg bx, cx
    %assign %%n %%n-16
  %endif
  %assign %%q %%n / 8
  %assign %%r %%n % 8
  %if %%r > 4
    %assign %%q %%q+1
    %assign %%r %%r-8
  %endif
  %if %%q == 1
    ROR8_32
  %elif %%q == 2
    xchg bx, cx
  %endif
  %if %%r > 0
    %rep %%r
      ROR1_32
    %endrep
  %elif %%r < 0
    %rep -%%r
      ROL1_32
    %endrep
  %endif
%endmacro
%macro SHR32 1                      ; logical shift BX:CX right by %1 (1..15)
  %assign %%n %1
  %if %%n >= 8
    mov cl, ch
    mov ch, bl
    mov bl, bh
    xor bh, bh
    %assign %%n %%n-8
  %endif
  %rep %%n
    shr bx, 1
    rcr cx, 1
  %endrep
%endmacro
%macro LD32 1                       ; BX:CX = the dword at [%1]
    mov cx, [%1]
    mov bx, [%1+2]
%endmacro
%macro XOR32 1                      ; [%1] ^= BX:CX
    xor [%1], cx
    xor [%1+2], bx
%endmacro
%macro ST32 1
    mov [%1], cx
    mov [%1+2], bx
%endmacro

; ---- SHA-256 ------------------------------------------------------------------
_pdc_sha256_block:
    push bp
    mov bp, sp
    push si
    push di
    mov ax, [bp+4]
    mov [s2st], ax
    mov si, [bp+6]
    mov di, s2w
    mov cx, 16
.ld:                                ; W[0..15], big-endian bytes
    mov ax, [si]
    xchg al, ah
    mov dx, [si+2]
    xchg dl, dh
    mov [di], dx                    ; lo word (bytes 2,3)
    mov [di+2], ax                  ; hi word (bytes 0,1)
    add si, 4
    add di, 4
    loop .ld
    mov si, s2w+64                  ; &W[16]
.ws:
    LD32 si-60                      ; sigma0(W[i-15]) = ror7 ^ ror18 ^ shr3
    ROTR32 7
    ST32 s2sg
    LD32 si-60
    ROTR32 18
    XOR32 s2sg
    LD32 si-60
    SHR32 3
    XOR32 s2sg
    LD32 si-8                       ; sigma1(W[i-2]) = ror17 ^ ror19 ^ shr10
    ROTR32 17
    ST32 s2sh
    LD32 si-8
    ROTR32 19
    XOR32 s2sh
    LD32 si-8
    SHR32 10
    XOR32 s2sh
    mov ax, [si-64]
    mov dx, [si-62]
    add ax, [si-28]
    adc dx, [si-26]
    add ax, [s2sg]
    adc dx, [s2sg+2]
    add ax, [s2sh]
    adc dx, [s2sh+2]
    mov [si], ax
    mov [si+2], dx
    add si, 4
    cmp si, s2w+256
    jae .wd
    jmp .ws
.wd:
    mov si, [s2st]
    mov di, s2v
    mov cx, 16
.cv:
    lodsw
    mov [di], ax
    inc di
    inc di
    loop .cv
    xor si, si                      ; round counter * 4
.rd:
    ; Sigma1(e) = ror6 ^ ror11 ^ ror25
    LD32 s2v+16
    ROTR32 6
    ST32 s2sg
    LD32 s2v+16
    ROTR32 11
    XOR32 s2sg
    LD32 s2v+16
    ROTR32 25
    XOR32 s2sg
    mov ax, [s2v+28]                ; T1 = h + Sigma1
    mov dx, [s2v+30]
    add ax, [s2sg]
    adc dx, [s2sg+2]
    mov bx, [s2v+20]                ; Ch = g ^ (e & (f ^ g))
    xor bx, [s2v+24]
    and bx, [s2v+16]
    xor bx, [s2v+24]
    mov cx, [s2v+22]
    xor cx, [s2v+26]
    and cx, [s2v+18]
    xor cx, [s2v+26]
    add ax, bx
    adc dx, cx
    add ax, [s2k+si]
    adc dx, [s2k+si+2]
    add ax, [s2w+si]
    adc dx, [s2w+si+2]
    mov [s2t1], ax
    mov [s2t1+2], dx
    ; Sigma0(a) = ror2 ^ ror13 ^ ror22
    LD32 s2v
    ROTR32 2
    ST32 s2sg
    LD32 s2v
    ROTR32 13
    XOR32 s2sg
    LD32 s2v
    ROTR32 22
    XOR32 s2sg
    mov bx, [s2v]                   ; Maj = (a & b) | (c & (a | b))
    mov ax, bx
    or ax, [s2v+4]
    and ax, [s2v+8]
    and bx, [s2v+4]
    or bx, ax
    mov cx, [s2v+2]
    mov ax, cx
    or ax, [s2v+6]
    and ax, [s2v+10]
    and cx, [s2v+6]
    or cx, ax
    mov ax, [s2sg]
    mov dx, [s2sg+2]
    add ax, bx
    adc dx, cx                      ; T2
    add ax, [s2t1]
    adc dx, [s2t1+2]                ; new a = T1 + T2
    mov bx, [s2v+12]                ; new e = d + T1
    mov cx, [s2v+14]
    add bx, [s2t1]
    adc cx, [s2t1+2]
    ; h=g g=f f=e(old) e=new_e d=c c=b b=a a=new_a
    mov di, [s2v+24]
    mov [s2v+28], di
    mov di, [s2v+26]
    mov [s2v+30], di
    mov di, [s2v+20]
    mov [s2v+24], di
    mov di, [s2v+22]
    mov [s2v+26], di
    mov di, [s2v+16]
    mov [s2v+20], di
    mov di, [s2v+18]
    mov [s2v+22], di
    mov [s2v+16], bx
    mov [s2v+18], cx
    mov di, [s2v+8]
    mov [s2v+12], di
    mov di, [s2v+10]
    mov [s2v+14], di
    mov di, [s2v+4]
    mov [s2v+8], di
    mov di, [s2v+6]
    mov [s2v+10], di
    mov di, [s2v]
    mov [s2v+4], di
    mov di, [s2v+2]
    mov [s2v+6], di
    mov [s2v], ax
    mov [s2v+2], dx
    add si, 4
    cmp si, 256
    jae .rdone
    jmp .rd
.rdone:
    mov si, [s2st]
    mov bx, s2v
    mov cx, 8
.fin:
    mov ax, [bx]
    add [si], ax
    mov ax, [bx+2]
    adc [si+2], ax
    add bx, 4
    add si, 4
    loop .fin
    pop di
    pop si
    pop bp
    ret

s2k:
        dd 0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5
        dd 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5
        dd 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3
        dd 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174
        dd 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc
        dd 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da
        dd 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7
        dd 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967
        dd 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13
        dd 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85
        dd 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3
        dd 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070
        dd 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5
        dd 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3
        dd 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208
        dd 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2

; ---- 64-bit values: four words, least first, in memory --------------------------
%macro MOV64 2                      ; [%1] = [%2]
    mov ax, [%2]
    mov [%1], ax
    mov ax, [%2+2]
    mov [%1+2], ax
    mov ax, [%2+4]
    mov [%1+4], ax
    mov ax, [%2+6]
    mov [%1+6], ax
%endmacro
%macro XOR64 2                      ; [%1] ^= [%2]
    mov ax, [%2]
    xor [%1], ax
    mov ax, [%2+2]
    xor [%1+2], ax
    mov ax, [%2+4]
    xor [%1+4], ax
    mov ax, [%2+6]
    xor [%1+6], ax
%endmacro
%macro ADD64 2                      ; [%1] += [%2]
    mov ax, [%2]
    add [%1], ax
    mov ax, [%2+2]
    adc [%1+2], ax
    mov ax, [%2+4]
    adc [%1+4], ax
    mov ax, [%2+6]
    adc [%1+6], ax
%endmacro
%macro ROTR64 3                     ; [%1] = [%2] rotated right by %3 (1..63); %1 != %2
  %assign %%q %3 / 16
  %assign %%r %3 % 16
  %assign %%j 0
  %rep 4
    %assign %%a ((%%j + %%q) % 4) * 2
    %assign %%b ((%%j + %%q + 1) % 4) * 2
    %if %%r == 0
      mov ax, [%2+%%a]
    %else
      mov ax, [%2+%%a]
      mov dx, [%2+%%b]
      mov cl, %%r
      shr ax, cl
      mov cl, 16-%%r
      shl dx, cl
      or ax, dx
    %endif
    mov [%1+%%j*2], ax
    %assign %%j %%j+1
  %endrep
%endmacro
%macro SHR64 3                      ; [%1] = [%2] >> %3 (logical, 1..63); %1 != %2
  %assign %%q %3 / 16
  %assign %%r %3 % 16
  %assign %%j 0
  %rep 4
    %assign %%a (%%j + %%q) * 2
    %if %%j + %%q > 3
      mov word [%1+%%j*2], 0
    %elif %%j + %%q == 3
      mov ax, [%2+%%a]
      %if %%r > 0
        mov cl, %%r
        shr ax, cl
      %endif
      mov [%1+%%j*2], ax
    %else
      mov ax, [%2+%%a]
      %if %%r > 0
        mov dx, [%2+%%a+2]
        mov cl, %%r
        shr ax, cl
        mov cl, 16-%%r
        shl dx, cl
        or ax, dx
      %endif
      mov [%1+%%j*2], ax
    %endif
    %assign %%j %%j+1
  %endrep
%endmacro

; ---- SHA-512 ------------------------------------------------------------------
_pdc_sha512_block:
    push bp
    mov bp, sp
    push si
    push di
    mov ax, [bp+4]
    mov [s2st], ax
    mov si, [bp+6]
    mov di, s5w
    mov cx, 16
.ld:                                ; W[0..15], big-endian
    mov ax, [si]
    xchg al, ah
    mov [di+6], ax
    mov ax, [si+2]
    xchg al, ah
    mov [di+4], ax
    mov ax, [si+4]
    xchg al, ah
    mov [di+2], ax
    mov ax, [si+6]
    xchg al, ah
    mov [di], ax
    add si, 8
    add di, 8
    loop .ld
    mov si, s5w+128                 ; &W[16]
.ws:
    ROTR64 s5sg, si-120, 1          ; sigma0(W[i-15]) = ror1 ^ ror8 ^ shr7
    ROTR64 s5sh, si-120, 8
    XOR64 s5sg, s5sh
    SHR64 s5sh, si-120, 7
    XOR64 s5sg, s5sh
    ROTR64 s5t1, si-16, 19          ; sigma1(W[i-2]) = ror19 ^ ror61 ^ shr6
    ROTR64 s5sh, si-16, 61
    XOR64 s5t1, s5sh
    SHR64 s5sh, si-16, 6
    XOR64 s5t1, s5sh
    ADD64 s5sg, s5t1
    ADD64 s5sg, si-128              ; W[i-16]
    ADD64 s5sg, si-56               ; W[i-7]
    MOV64 si, s5sg
    add si, 8
    cmp si, s5w+640
    jae .wd
    jmp .ws
.wd:
    mov si, [s2st]
    mov di, s5v
    mov cx, 32
.cv:
    lodsw
    mov [di], ax
    inc di
    inc di
    loop .cv
    xor si, si                      ; round * 8
.rd:
    ROTR64 s5sg, s5v+32, 14         ; Sigma1(e)
    ROTR64 s5sh, s5v+32, 18
    XOR64 s5sg, s5sh
    ROTR64 s5sh, s5v+32, 41
    XOR64 s5sg, s5sh
    MOV64 s5t1, s5v+56              ; T1 = h + Sigma1
    ADD64 s5t1, s5sg
    mov cx, 4                       ; Ch = g ^ (e & (f ^ g)), a word at a time
    mov bx, 0
.ch:
    mov ax, [s5v+40+bx]
    xor ax, [s5v+48+bx]
    and ax, [s5v+32+bx]
    xor ax, [s5v+48+bx]
    mov [s5sh+bx], ax
    inc bx
    inc bx
    loop .ch
    ADD64 s5t1, s5sh
    ADD64 s5t1, s5k+si
    ADD64 s5t1, s5w+si
    ROTR64 s5sg, s5v, 28            ; Sigma0(a)
    ROTR64 s5sh, s5v, 34
    XOR64 s5sg, s5sh
    ROTR64 s5sh, s5v, 39
    XOR64 s5sg, s5sh
    mov cx, 4                       ; Maj = (a & b) | (c & (a | b))
    mov bx, 0
.mj:
    mov ax, [s5v+bx]
    mov dx, ax
    or ax, [s5v+8+bx]
    and ax, [s5v+16+bx]
    and dx, [s5v+8+bx]
    or ax, dx
    mov [s5sh+bx], ax
    inc bx
    inc bx
    loop .mj
    ADD64 s5sg, s5sh                ; T2
    ADD64 s5sg, s5t1                ; new a
    ADD64 s5v+24, s5t1              ; new e (in d's slot)
    MOV64 s5v+56, s5v+48            ; h = g
    MOV64 s5v+48, s5v+40            ; g = f
    MOV64 s5v+40, s5v+32            ; f = e
    MOV64 s5v+32, s5v+24            ; e = new e
    MOV64 s5v+24, s5v+16            ; d = c
    MOV64 s5v+16, s5v+8             ; c = b
    MOV64 s5v+8, s5v                ; b = a
    MOV64 s5v, s5sg                 ; a = new a
    add si, 8
    cmp si, 640
    jae .rdone
    jmp .rd
.rdone:
    mov si, [s2st]
    mov bx, s5v
    mov cx, 8
.fin:
    mov ax, [bx]
    add [si], ax
    mov ax, [bx+2]
    adc [si+2], ax
    mov ax, [bx+4]
    adc [si+4], ax
    mov ax, [bx+6]
    adc [si+6], ax
    add bx, 8
    add si, 8
    loop .fin
    pop di
    pop si
    pop bp
    ret

align 2, db 0
s5k:
        dq 0x428a2f98d728ae22, 0x7137449123ef65cd, 0xb5c0fbcfec4d3b2f, 0xe9b5dba58189dbbc
        dq 0x3956c25bf348b538, 0x59f111f1b605d019, 0x923f82a4af194f9b, 0xab1c5ed5da6d8118
        dq 0xd807aa98a3030242, 0x12835b0145706fbe, 0x243185be4ee4b28c, 0x550c7dc3d5ffb4e2
        dq 0x72be5d74f27b896f, 0x80deb1fe3b1696b1, 0x9bdc06a725c71235, 0xc19bf174cf692694
        dq 0xe49b69c19ef14ad2, 0xefbe4786384f25e3, 0x0fc19dc68b8cd5b5, 0x240ca1cc77ac9c65
        dq 0x2de92c6f592b0275, 0x4a7484aa6ea6e483, 0x5cb0a9dcbd41fbd4, 0x76f988da831153b5
        dq 0x983e5152ee66dfab, 0xa831c66d2db43210, 0xb00327c898fb213f, 0xbf597fc7beef0ee4
        dq 0xc6e00bf33da88fc2, 0xd5a79147930aa725, 0x06ca6351e003826f, 0x142929670a0e6e70
        dq 0x27b70a8546d22ffc, 0x2e1b21385c26c926, 0x4d2c6dfc5ac42aed, 0x53380d139d95b3df
        dq 0x650a73548baf63de, 0x766a0abb3c77b2a8, 0x81c2c92e47edaee6, 0x92722c851482353b
        dq 0xa2bfe8a14cf10364, 0xa81a664bbc423001, 0xc24b8b70d0f89791, 0xc76c51a30654be30
        dq 0xd192e819d6ef5218, 0xd69906245565a910, 0xf40e35855771202a, 0x106aa07032bbd1b8
        dq 0x19a4c116b8d2d0c8, 0x1e376c085141ab53, 0x2748774cdf8eeb99, 0x34b0bcb5e19b48a8
        dq 0x391c0cb3c5c95a63, 0x4ed8aa4ae3418acb, 0x5b9cca4f7763e373, 0x682e6ff3d6b2b8a3
        dq 0x748f82ee5defb2fc, 0x78a5636f43172f60, 0x84c87814a1f0ab72, 0x8cc702081a6439ec
        dq 0x90befffa23631e28, 0xa4506cebde82bde9, 0xbef9a3f7b2c67915, 0xc67178f2e372532b
        dq 0xca273eceea26619c, 0xd186b8c721c0c207, 0xeada7dd6cde0eb1e, 0xf57d4f7fee6ed178
        dq 0x06f067aa72176fba, 0x0a637dc5a2c898a6, 0x113f9804bef90dae, 0x1b710b35131c471b
        dq 0x28db77f523047d84, 0x32caab7b40c72493, 0x3c9ebe0a15c9bebc, 0x431d67c49c100d4c
        dq 0x4cc5d4becb3e42b6, 0x597f299cfc657e2a, 0x5fcb6fab3ad6faec, 0x6c44198c4a475817

%endif
