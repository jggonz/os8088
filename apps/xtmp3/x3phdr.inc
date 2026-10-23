; =============================================================================
; os8088 - apps/xtmp3/x3phdr.inc - XTMP3's frame header (x3sim.parse_header)
;
; SHARED AS SOURCE by x3pars.inc (the open, in the primary) and x3front.asm
; (the frame loop, x3pnext.inc, in the FRONT part): one header decoder, two
; assemblies. It needs X3FTAB_PARSE's x3p_tbr and x3p_tsr where it lands.
; =============================================================================
; THE HEADER (x3sim.parse_header), answered in registers so that nothing is
; shared between a probe on the UI task and the worker's stream
;
; x3p_hdr - the four bytes DH DL AH AL (in file order)
; out: CF = 1 not a header we could believe. CF = 0: AL = version (1, 2, 25),
;      AH = layer 1..3, BL = rate index, BH = bitrate index, CL = channels,
;      CH = misc (X3PH_*: bits 0-1 mode, 2-3 mode extension), DX = the
;      frame's bytes (0 for free format)
; clobbers: nothing else
; =============================================================================
X3PH_CRC    equ 0x10                ; a CRC word follows the header
X3PH_PAD    equ 0x20
X3PH_FREE   equ 0x40

x3p_hdr:
    push si
    push di
    cmp dh, 0xFF                    ; sync: eleven ones
    jne .no
    mov si, dx                      ; SI = b0 b1, DI = b2 b3
    mov di, ax
    mov al, dl
    and al, 0xE0
    cmp al, 0xE0
    jne .no
    mov al, dl                      ; vb = (b1 >> 3) & 3
    shr al, 1
    shr al, 1
    shr al, 1
    and al, 3
    cmp al, 1
    je .no
    mov ah, dl                      ; lb = (b1 >> 1) & 3
    shr ah, 1
    and ah, 3
    jz .no
    mov bx, di                      ; b2: br = b2 >> 4, sr = (b2 >> 2) & 3
    mov bl, bh
    mov cl, 4
    shr bh, cl
    cmp bh, 15
    je .no
    shr bl, 1
    shr bl, 1
    and bl, 3
    cmp bl, 3
    je .no
    mov cl, 25                      ; version: vb 0 -> 25, 2 -> 2, 3 -> 1
    or al, al
    jz .v
    mov cl, 2
    cmp al, 2
    je .v
    mov cl, 1
.v:
    mov al, cl
    mov cl, 4                       ; layer = 4 - lb
    sub cl, ah
    mov ah, cl
    mov cx, di                      ; b3: mode = b3 >> 6, mext = (b3 >> 4) & 3
    mov ch, cl
    shr ch, 1
    shr ch, 1
    shr ch, 1
    shr ch, 1
    mov cl, ch
    and cl, 3
    shr ch, 1
    shr ch, 1
    shl cl, 1
    shl cl, 1
    or ch, cl                       ; CH = mode | mext << 2
    mov dx, si
    test dl, 1                      ; protection_bit 0: a CRC follows
    jnz .nc
    or ch, X3PH_CRC
.nc:
    mov dx, di
    test dh, 2                      ; padding: b2 bit 1
    jz .np
    or ch, X3PH_PAD
.np:
    or bh, bh
    jnz .nf
    or ch, X3PH_FREE
.nf:
    mov cl, 2
    mov dl, ch
    and dl, 3
    cmp dl, 3
    jne .st
    mov cl, 1                       ; mode 3: mono
.st:
    xor dx, dx
    test ch, X3PH_FREE
    jnz .ok
    call x3p_fsize
.ok:
    pop di
    pop si
    clc
    ret
.no:
    pop di
    pop si
    stc
    ret

; x3p_fsize - x3p_hdr's AL AH BL BH CH -> DX = the frame's bytes. keeps the
; rest but SI, DI
x3p_fsize:
    push ax
    push bx
    push cx
    push ax
    call x3p_kbps
    mov si, ax
    pop ax
    push ax
    call x3p_rate
    mov di, ax
    pop ax
    push ax
    mov cx, 12                      ; layer 1: (12 kbps 1000 / rate + pad) 4
    cmp ah, 1
    je .m
    mov cx, 144                     ; layer 2 or MPEG-1: 144 kbps 1000 / rate
    cmp ah, 2
    je .m
    cmp al, 1
    je .m
    mov cx, 72                      ; MPEG-2/2.5 layer 3: 72 kbps 1000 / rate
.m:
    mov ax, si
    mul cx                          ; (<= 144 x 448 = 64,512)
    mov cx, 1000
    mul cx
    div di
    pop dx                          ; DL = version, DH = layer
    pop cx
    push cx
    test ch, X3PH_PAD
    jz .p
    inc ax
.p:
    cmp dh, 1
    jne .e
    shl ax, 1
    shl ax, 1
.e:
    mov dx, ax
    pop cx
    pop bx
    pop ax
    ret

; x3p_kbps - AL = version, AH = layer, BH = bitrate index -> AX = kbps
x3p_kbps:
    push bx
    mov bl, ah                      ; row: (MPEG-1 ? 0 : 3) + layer - 1
    dec bl
    cmp al, 1
    je .r
    add bl, 3
.r:
    mov al, 15
    mul bl
    mov bl, bh
    xor bh, bh
    add bx, ax
    shl bx, 1
    mov ax, [x3p_tbr+bx]
    pop bx
    ret

; x3p_rate - AL = version, BL = rate index -> AX = Hz
x3p_rate:
    push bx
    call x3p_vi                     ; AL = vi
    mov ah, 3
    mul ah
    xor bh, bh
    add bx, ax
    shl bx, 1
    mov ax, [x3p_tsr+bx]
    pop bx
    ret

; x3p_vi - AL = version (1, 2, 25) -> AL = 0, 1, 2
x3p_vi:
    cmp al, 1
    je .z
    cmp al, 2
    je .o
    mov al, 2
    ret
.o:
    mov al, 1
    ret
.z:
    xor al, al
    ret

; x3p_sideb - AL = version, CL = channels -> AX = side-information bytes
x3p_sideb:
    cmp al, 1
    jne .m2
    mov ax, 17
    cmp cl, 1
    je .o
    mov ax, 32
    ret
.m2:
    mov ax, 9
    cmp cl, 1
    je .o
    mov ax, 17
.o:
    ret

