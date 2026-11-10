; =============================================================================
; os8088 - apps/pagedesk/pdcrc4.inc                RC4, 8086 (PAGEDESK-SPEC 20.22.5)
;
; _pdc_rc4_ks(s, key, n)   s: 258 bytes (S[256], i, j); key n = 1..32 bytes
; _pdc_rc4(s, buf, n)      in place, n bytes; the state carries between calls
; SmallerC cdecl, near pointers into DS; AX BX CX DX lost, BP SI DI kept.
; (BP is used as a data pointer inside, with an explicit DS override.)
; =============================================================================
%ifndef PDCRC4_INC
%define PDCRC4_INC 1
cpu 8086

section .bss
rc4ke:  resw 1
section .text

_pdc_rc4_ks:
    push bp
    mov bp, sp
    push si
    push di
    mov bx, [bp+4]                  ; s
    mov dx, [bp+6]                  ; key base
    mov ax, dx
    add ax, [bp+8]
    mov [rc4ke], ax                 ; key end
    xor si, si
.init:
    mov ax, si
    mov [bx+si], al
    inc si
    cmp si, 256
    jb .init
    mov byte [bx+256], 0
    mov byte [bx+257], 0
    mov bp, dx                      ; BP = key pointer
    xor si, si                      ; i
    xor di, di                      ; j
.mix:
    mov al, [bx+si]
    xor ah, ah
    add di, ax
    mov al, [ds:bp]
    add di, ax
    and di, 255
    mov al, [bx+si]
    mov ah, [bx+di]
    mov [bx+si], ah
    mov [bx+di], al
    inc bp
    cmp bp, [rc4ke]
    jb .nw
    mov bp, dx
.nw:
    inc si
    cmp si, 256
    jb .mix
    pop di
    pop si
    pop bp
    ret

_pdc_rc4:
    push bp
    mov bp, sp
    push si
    push di
    mov bx, [bp+4]                  ; s
    mov cx, [bp+8]                  ; n
    mov bp, [bp+6]                  ; BP = buf (the frame is gone)
    or cx, cx
    jz .done
    xor ah, ah
    mov al, [bx+256]
    mov si, ax                      ; i
    mov al, [bx+257]
    mov di, ax                      ; j
.lp:
    inc si
    and si, 255
    mov al, [bx+si]
    xor ah, ah
    add di, ax
    and di, 255
    mov ah, [bx+di]
    mov [bx+si], ah
    mov [bx+di], al
    add al, ah
    xlat                            ; AL = S[(Si + Sj) & 255]
    xor [ds:bp], al
    inc bp
    loop .lp
    mov ax, si
    mov [bx+256], al
    mov ax, di
    mov [bx+257], al
.done:
    pop di
    pop si
    pop bp
    ret
%endif
