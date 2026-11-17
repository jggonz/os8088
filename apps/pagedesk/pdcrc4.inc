; =============================================================================
; os8088 - apps/pagedesk/pdcrc4.inc                RC4, 8086 (PAGEDESK-SPEC 20.22.5)
;
; _pdc_rc4_ks(s, key, n)   s: 258 bytes (S[256], i, j); key n = 1..32 bytes
; _pdc_rc4(s, buf, n)      in place, n bytes; the state carries between calls
; SmallerC cdecl, near pointers into DS (PDC_FARST: the state s is an offset in the segment _pdc_cseg); AX BX CX DX lost, BP SI DI kept.
; (BP is used as a data pointer inside, with an explicit DS override.)
; =============================================================================
%ifndef PDCRC4_INC
%define PDCRC4_INC 1
cpu 8086

section .bss
rc4ke:  resw 1
%ifdef PDC_FARST
%ifndef PDC_CSEG_DEF
%define PDC_CSEG_DEF 1
_pdc_cseg: resw 1                   ; the segment of the cipher area (see pdcaes.inc)
%endif
%define RKP es:
%define RC4XL db 0x26
%macro RC4_ES 0
    push es
    mov es, [_pdc_cseg]
%endmacro
%macro RC4_ESX 0
    pop es
%endmacro
%else
%define RKP
%define RC4XL
%macro RC4_ES 0
%endmacro
%macro RC4_ESX 0
%endmacro
%endif
section .text

_pdc_rc4_ks:
    push bp
    mov bp, sp
    push si
    push di
    RC4_ES
    mov bx, [bp+4]                  ; s
    mov dx, [bp+6]                  ; key base
    mov ax, dx
    add ax, [bp+8]
    mov [rc4ke], ax                 ; key end
    xor si, si
.init:
    mov ax, si
    mov [RKP bx+si], al
    inc si
    cmp si, 256
    jb .init
    mov byte [RKP bx+256], 0
    mov byte [RKP bx+257], 0
    mov bp, dx                      ; BP = key pointer
    xor si, si                      ; i
    xor di, di                      ; j
.mix:
    mov al, [RKP bx+si]
    xor ah, ah
    add di, ax
    mov al, [ds:bp]
    add di, ax
    and di, 255
    mov al, [RKP bx+si]
    mov ah, [RKP bx+di]
    mov [RKP bx+si], ah
    mov [RKP bx+di], al
    inc bp
    cmp bp, [rc4ke]
    jb .nw
    mov bp, dx
.nw:
    inc si
    cmp si, 256
    jb .mix
    RC4_ESX
    pop di
    pop si
    pop bp
    ret

_pdc_rc4:
    push bp
    mov bp, sp
    push si
    push di
    RC4_ES
    mov bx, [bp+4]                  ; s
    mov cx, [bp+8]                  ; n
    mov bp, [bp+6]                  ; BP = buf (the frame is gone)
    or cx, cx
    jz .done
    xor ah, ah
    mov al, [RKP bx+256]
    mov si, ax                      ; i
    mov al, [RKP bx+257]
    mov di, ax                      ; j
.lp:
    inc si
    and si, 255
    mov al, [RKP bx+si]
    xor ah, ah
    add di, ax
    and di, 255
    mov ah, [RKP bx+di]
    mov [RKP bx+si], ah
    mov [RKP bx+di], al
    add al, ah
    RC4XL
    xlat                            ; AL = S[(Si + Sj) & 255]
    xor [ds:bp], al
    inc bp
    loop .lp
    mov ax, si
    mov [RKP bx+256], al
    mov ax, di
    mov [RKP bx+257], al
.done:
    RC4_ESX
    pop di
    pop si
    pop bp
    ret
%endif
