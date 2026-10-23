; =============================================================================
; os8088 - apps/xtmp3/x3crc.inc - CRC-32 (zlib's) for the XTMP3 parts' OPEN
;
; Shared as SOURCE by the parts that check their tables against
; tools/x3tables.py's CRC-32s (x3std.inc's X3T_*_CRC): the FRONT part's
; Huffman tables, the STANDARD part's blob. A nibble table (64 bytes), two
; lookups a byte: about 25 cycles a byte more than a 1 KB table would cost,
; and 960 bytes less. Included in .text; it is code and its table.
;
; x3g_crc32 - continue a CRC-32 over CX bytes at DS:SI
; in:  DX:AX = the running CRC (0FFFFFFFFh to start; XOR the end with
;      0FFFFFFFFh yourself), DS:SI = the bytes, CX = how many (0 = none)
; out: DX:AX = the running CRC, SI advanced
; clobbers: BX, CX, DI, flags
; =============================================================================

x3g_crc32:
    jcxz .out
.byte:
    mov bl, [si]
    inc si
    xor bl, al                      ; the low nibble first: crc ^ byte
    call .nib
    mov bl, [si-1]
    mov bh, cl                      ; (keep the count)
    mov cl, 4
    shr bl, cl
    mov cl, bh
    xor bl, al                      ; ...then the high nibble
    call .nib
    loop .byte
.out:
    ret
.nib:                               ; crc = (crc >> 4) ^ T[(crc ^ n) & 15]
    and bx, 15
    shl bx, 1
    shl bx, 1
    mov di, bx
    push cx
    mov cx, 4
.sh:
    shr dx, 1
    rcr ax, 1
    loop .sh
    pop cx
    xor ax, [x3g_crct + di]
    xor dx, [x3g_crct + di + 2]
    ret

x3g_crct:
    dd 0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC
    dd 0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C
    dd 0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C
    dd 0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C
