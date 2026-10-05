; =============================================================================
; os8088 - apps/pixel/pxwrite.asm
;
; PiXEL's WRITE PART (SPEC.md 106.24): part 8 of PIXEL.O88, LINKED against
; the package (build/pxlink.inc, SPEC.md 106.20) - File > Save As's five
; writers: BMP (8-bit, and 24-bit on a CUBE master), PCX (8-bit), GIF (the
; LZW encoder of apps/os88lzw.inc), PNG (a zlib stream of one fixed-Huffman
; deflate block over a hash-chain LZ77, its CRC-32s and its Adler-32) and
; PIX (os8088's own archive, SPEC.md 61.7).
;
; IT RUNS ON THE WORKER AND NEVER TOUCHES A FILE (SPEC.md 20.6 rule 7): the
; encoded bytes go into the RING's slots - px_ringclaim's two, each a
; whole number of clusters - and the UI task writes each full one from
; W_ONWAKE, the decode pump's handshake run backwards (SPEC.md 106.9): a
; slot's length, then its `full` byte, then the request byte LAST and a
; wake; the worker waits on its next slot's `full` byte with
; OSAPI_TASK_SLEEP - never OSAPI_TASK_ALIVE, which is where a compaction may
; move the master (SPEC.md 66.5) and a writer holds a pointer into a row
; across a slot. Every row ends at pw_tick, which IS the liveness poll, the
; progress ([px_erow]), a wake every three ticks and the cancel: an abort
; (Esc, or the UI task's write refused) unwinds the whole encoder from any
; depth to DECODE's own return (pw_abort).
;
; One vector, DECODE, a format in CL (apps/pixel/pxed.inc's WF_*); WF_SIZE |
; a format, on the UI task, says before anything is claimed whether the
; master can be written in it at all. tools/pixelsim.py's write_* are this
; file in Python and tests/pxsave.py holds the two to the byte - the GIF's
; and the PNG's compressed bytes too, because greedy LZW's output is its
; table policy's alone and the deflate below is pixelsim's deflate_fixed
; rule for rule.
; =============================================================================

%include "pxpart.inc"
%include "os88api.inc"
%include "pxrec.inc"
%include "pxed.inc"
%include "pxsvc.inc"
%include "pxlink.inc"

    cpu 8086
    bits 16
    org 0

    PXPART_HEAD pw_init, pw_decode, pw_none, pw_none, pw_none

pw_init:
    mov ax, PXP_PROBE
    clc
    retf

pw_none:
    mov ax, PXE_NOTSUP
    stc
    retf

; PXV_DECODE - CL = a format (| WF_SIZE), DS = the package. Preserves all
; but AX
pw_decode:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push bp
    cmp word [8], PXL_IMAGE         ; THE STAMP (SPEC.md 106.20)
    jne .link
    cmp word [10], PXL_BSS
    jne .link
    cld
    push ax
    mov ax, [px_svgp]               ; the resident's UI services (pxsvc.inc)
    mov [cs:pv_sv], ax
    mov ax, [cs:PXP_PKG]
    mov [cs:pv_sv + 2], ax
    pop ax                          ; (a UI verb's AX is its argument)
    test cl, 0x40
    jz .enc
    call pu_ui                      ; the UI task's half
    jmp pw_dret
.enc:
    test cl, WF_SIZE
    jnz .size
    cmp cl, WF_N
    jae .bad
    mov [cs:pw_sp], sp              ; where pw_abort unwinds to
    mov ax, cs                      ; the scratch's own segment
    add ax, PW_SCRP
    mov [cs:pw_dseg], ax
    mov word [px_erow], 0
    mov byte [cs:pw_gbn], 0
    xor ax, ax                      ; THE RECT: the whole master (a copy's
    mov [cs:pw_x0], ax              ; rect went with the BMP on the clipboard,
    mov [cs:pw_y0], ax              ; SPEC.md 106.25)
    mov ax, [px_cur + PXR_MW]
    mov [cs:pw_w], ax
    mov ax, [px_cur + PXR_MH]
    mov [cs:pw_h], ax
    call pw_slot                    ; the first slot
    mov bl, cl
    xor bh, bh
    shl bx, 1
    call [cs:pw_fmts + bx]
    call pw_end
    xor ax, ax
    clc
    jmp short pw_dret
.size:
    and cl, 0x7F
    xor ax, ax
    cmp cl, WF_PIX                  ; a PIX block is one segment (SPEC.md
    jne .ok                         ; 61.7): (w + 1) / 2 x h at most 65,535
    mov ax, [px_cur + PXR_MW]
    inc ax
    shr ax, 1
    mul word [px_cur + PXR_MH]
    or dx, dx
    jnz .pixbig
    cmp ax, 0xFFFF
    je .pixbig
    xor ax, ax
.ok:
    clc
    jmp short pw_dret
.pixbig:
    mov ax, PXW_PIXBIG
    stc
    jmp short pw_dret
.bad:
    mov ax, PXE_NOTSUP
    stc
    jmp short pw_dret
.link:
    mov ax, PXD_LINK
    stc
pw_dret:
    pop bp
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    retf

; pw_abort - the user said stop, or the UI task's write was refused: back to
; DECODE's return from wherever the encoder was, PXD_ABORT
pw_abort:
    ; STKBALANCE-OK: the encoder's way out from any depth - SP is put back
    ; to pw_decode's own frame from [pw_sp], so what was pushed between
    ; there and here is abandoned (pg_fail's shape); pw_dret's retf is
    ; pw_decode's
    mov sp, [cs:pw_sp]
    mov ds, [cs:PXP_PKG]
    cld
    mov ax, PXD_ABORT
    stc
    jmp short pw_dret

pw_fmts:    dw pw_png, pw_gif, pw_bmp8, pw_pcx, pw_pix, pw_bmp24

; =============================================================================
; THE SLOTS (the worker's end of the handshake)
; =============================================================================

; pw_slot - the output at the start of slot [px_wslot]. DS = the package.
; Preserves all
pw_slot:
    push ax
    push bx
    mov bl, [px_wslot]
    xor bh, bh
    shl bx, 1
    mov ax, [px_sseg + bx]
    mov [cs:pw_ptr + 2], ax
    mov word [cs:pw_ptr], 0
    mov ax, [px_chunk]
    mov [cs:pw_lim], ax
    pop bx
    pop ax
    ret

; pw_put - AL, the next byte of the file. Any DS, any ES. Preserves all
pw_put:
    push di
    push es
    les di, [cs:pw_ptr]
    stosb
    mov [cs:pw_ptr], di
    cmp di, [cs:pw_lim]
    jb .r
    call pw_flush
.r:
    pop es
    pop di
    ret

; pw_flush - the slot is handed to the UI task (its length, its full byte,
; the request LAST, a wake) and the next one waited for: SLEEP, not ALIVE.
; Any DS. Preserves all
pw_flush:
    push ax
    push bx
    push ds
    mov ds, [cs:PXP_PKG]
    mov bl, [px_wslot]
    xor bh, bh
    mov ax, [cs:pw_ptr]
    shl bx, 1
    mov [px_slen + bx], ax
    shr bx, 1
    mov byte [px_sfull + bx], 1
    mov byte [px_rq], 1             ; LAST
    push bx
    mov bx, [px_win]
    call OSAPI_WM_WAKE
    pop bx
    inc bl
    cmp bl, [px_nslot]
    jb .s
    xor bl, bl
.s:
    mov [px_wslot], bl
.w:
    cmp byte [px_abort], 0
    jne .ab
    cmp byte [px_sfull + bx], 0
    je .free
    mov ax, 1
    call OSAPI_TASK_SLEEP
    jmp short .w
.free:
    call pw_slot
    pop ds
    pop bx
    pop ax
    ret
.ab:
    jmp pw_abort

; pw_end - the last slot handed over with what it holds; no wait
pw_end:
    push ax
    push bx
    mov ax, [cs:pw_ptr]
    or ax, ax
    jz .o
    mov bl, [px_wslot]
    xor bh, bh
    shl bx, 1
    mov [px_slen + bx], ax
    shr bx, 1
    mov byte [px_sfull + bx], 1
    mov byte [px_rq], 1
    mov bx, [px_win]
    call OSAPI_WM_WAKE
.o:
    pop bx
    pop ax
    ret

; pw_tick - a row encoded: [px_erow], liveness (no pointer into a master is
; held here), a wake every three ticks, the cancel. DS = the package.
; Preserves all
pw_tick:
    push ax
    push bx
    inc word [px_erow]
    mov bx, [px_win]
    call OSAPI_TASK_ALIVE
    call OSAPI_GET_TICKS
    mov bx, ax
    sub ax, [px_etick]
    cmp ax, 3
    jb .w
    mov [px_etick], bx
    mov bx, [px_win]
    call OSAPI_WM_WAKE
.w:
    cmp byte [px_abort], 0
    jne .ab
    pop bx
    pop ax
    ret
.ab:
    jmp pw_abort

; --- a few byte shapes ---------------------------------------------------------
pw_putw:                            ; AX, little-endian. Preserves all
    call pw_put
    xchg al, ah
    call pw_put
    xchg al, ah
    ret
pw_putd:                            ; DX:AX, little-endian. Preserves all
    call pw_putw
    xchg ax, dx
    call pw_putw
    xchg ax, dx
    ret
pw_putbe:                           ; DX:AX, big-endian. Preserves all
    xchg ax, dx
    xchg al, ah
    call pw_put
    xchg al, ah
    call pw_put
    xchg ax, dx
    xchg al, ah
    call pw_put
    xchg al, ah
    call pw_put
    ret
pw_zeros:                           ; CX zero bytes. Preserves all
    push ax
    push cx
    xor al, al
    jcxz .o
.l:
    call pw_put
    loop .l
.o:
    pop cx
    pop ax
    ret
pw_puts:                            ; CS:SI, CX bytes. Preserves all
    push ax
    push cx
    push si
    jcxz .o
.l:
    mov al, [cs:si]
    call pw_put
    inc si
    loop .l
.o:
    pop si
    pop cx
    pop ax
    ret

; pw_row - AX = a row of the picture being written (the selection's, for a
; copy): DX:SI = its first pixel in the master. Preserves all else
pw_row:
    push ax
    push cx
    add ax, [cs:pw_y0]
    mul word [px_cur + PXR_MW]
    add ax, [cs:pw_x0]
    adc dx, 0
    mov si, ax
    and si, 15
    mov cl, 4
    shr ax, cl
    mov cl, 12
    shl dx, cl
    or ax, dx
    add ax, [px_cur + PXR_MSEG]     ; re-read: the master may have moved
    mov dx, ax
    pop cx
    pop ax
    ret

; =============================================================================
; BMP - 8-bit (pixelsim's write_bmp8) and 24-bit (write_bmp24)
; =============================================================================
pw_bmp8:
    mov ax, [cs:pw_w]
    add ax, 3
    and ax, 0xFFFC
    mov [cs:pw_stride], ax
    mov si, 54 + 1024
    mov bl, 8
    call pw_bmphdr
    mov si, px_pal                  ; the palette: B, G, R, 0
    mov cx, 256
.p:
    mov al, [si + 2]
    call pw_put
    mov al, [si + 1]
    call pw_put
    mov al, [si]
    call pw_put
    xor al, al
    call pw_put
    add si, 3
    loop .p
    mov bp, [cs:pw_h]       ; the rows, bottom up
.r:
    dec bp
    js .done
    mov ax, bp
    call pw_row
    mov cx, [cs:pw_w]
    push ds
    mov ds, dx
.x:
    lodsb
    call pw_put
    loop .x
    pop ds
    mov cx, [cs:pw_stride]
    sub cx, [cs:pw_w]
    call pw_zeros
    call pw_tick
    jmp short .r
.done:
    ret

pw_bmp24:
    mov ax, [cs:pw_w]
    call pw_x3
    add ax, 3
    and ax, 0xFFFC
    mov [cs:pw_stride], ax
    mov si, 54
    mov bl, 24
    call pw_bmphdr
    mov bp, [cs:pw_h]
.r:
    dec bp
    js .done
    mov ax, bp
    call pw_row
    mov es, dx
    mov cx, [cs:pw_w]
.x:
    mov bl, [es:si]
    inc si
    xor bh, bh
    mov ax, bx
    shl bx, 1
    add bx, ax
    mov al, [px_pal + bx + 2]
    call pw_put
    mov al, [px_pal + bx + 1]
    call pw_put
    mov al, [px_pal + bx]
    call pw_put
    loop .x
    mov ax, [cs:pw_w]
    call pw_x3
    mov cx, [cs:pw_stride]
    sub cx, ax
    call pw_zeros
    call pw_tick
    jmp short .r
.done:
    ret

; pw_x3 - AX = 3 AX. Preserves all else
pw_x3:
    push dx
    mov dx, ax
    shl ax, 1
    add ax, dx
    pop dx
    ret

; pw_bmphdr - the 54 bytes: SI = the pixels' offset, BL = the bits, the
; stride in [pw_stride]. The image is stride x h, the colours used 256 for
; 8 bits and 0 for 24. Preserves BP
pw_bmphdr:
    mov ax, [cs:pw_stride]          ; DX:AX = the image's bytes
    mul word [cs:pw_h]
    mov [cs:pw_img], ax
    mov [cs:pw_img + 2], dx
    mov al, 'B'
    call pw_put
    mov al, 'M'
    call pw_put
    mov ax, [cs:pw_img]             ; the file's size
    mov dx, [cs:pw_img + 2]
    add ax, si
    adc dx, 0
    call pw_putd
    xor ax, ax
    xor dx, dx
    call pw_putd
    mov ax, si
    call pw_putd
    mov ax, 40
    call pw_putd
    mov ax, [cs:pw_w]
    call pw_putd
    mov ax, [cs:pw_h]
    call pw_putd
    mov ax, 1
    call pw_putw
    mov al, bl
    xor ah, ah
    call pw_putw
    xor ax, ax
    call pw_putd
    mov ax, [cs:pw_img]
    mov dx, [cs:pw_img + 2]
    call pw_putd
    mov ax, 2835
    xor dx, dx
    call pw_putd
    call pw_putd
    xor ax, ax
    cmp bl, 8
    jne .c
    mov ax, 256
.c:
    call pw_putd
    xor ax, ax
    call pw_putd
    ret

; =============================================================================
; PCX - 8-bit, one plane, RLE (write_pcx8): each row copied to a line of
; bpl bytes (even, the pad 0) and its runs of at most 63 written
; =============================================================================
pw_pcx:
    mov ax, [cs:pw_w]
    inc ax
    and ax, 0xFFFE
    mov [cs:pw_stride], ax          ; bpl
    mov al, 10
    call pw_put
    mov al, 5
    call pw_put
    mov al, 1
    call pw_put
    mov al, 8
    call pw_put
    xor ax, ax
    call pw_putw                    ; xmin, ymin
    call pw_putw
    mov ax, [cs:pw_w]
    dec ax
    call pw_putw
    mov ax, [cs:pw_h]
    dec ax
    call pw_putw
    mov ax, 72
    call pw_putw
    call pw_putw
    mov cx, 49                      ; the EGA palette and the reserved byte
    call pw_zeros
    mov al, 1
    call pw_put                     ; planes
    mov ax, [cs:pw_stride]
    call pw_putw
    mov ax, 1
    call pw_putw                    ; palette info
    mov cx, 128 - 70
    call pw_zeros
    xor bp, bp
.r:
    cmp bp, [cs:pw_h]
    jae .tail
    mov ax, bp                      ; the row into the line, padded
    call pw_row
    mov es, [cs:pw_dseg]
    xor di, di
    mov cx, [cs:pw_w]
    push ds
    mov ds, dx
    rep movsb
    pop ds
    mov byte [es:di], 0
    push ds
    mov ds, [cs:pw_dseg]            ; DS = the line
    xor si, si
    mov dx, [cs:pw_stride]          ; DX = bpl
.run:
    cmp si, dx
    jae .rd
    mov al, [si]
    mov cx, 1                       ; CX = the run
.n:
    mov bx, si
    add bx, cx
    cmp bx, dx
    jae .e
    cmp cx, 63
    jae .e
    cmp [bx], al
    jne .e
    inc cx
    jmp short .n
.e:
    cmp cx, 1
    ja .pair
    cmp al, 0xC0
    jb .lit
.pair:
    push ax
    mov al, cl
    or al, 0xC0
    call pw_put
    pop ax
.lit:
    call pw_put
    add si, cx
    jmp short .run
.rd:
    pop ds
    call pw_tick
    inc bp
    jmp short .r
.tail:
    mov al, 12
    call pw_put
    mov si, px_pal
    mov cx, 768
.p:
    lodsb
    call pw_put
    loop .p
    ret

; =============================================================================
; GIF87a - the screen the picture, the global table the palette, one image,
; LZW of minimum size 8 in sub-blocks of 255 (write_gif)
; =============================================================================
pw_gif:
    mov si, pw_s_gif
    mov cx, 6
    call pw_puts
    mov ax, [cs:pw_w]
    call pw_putw
    mov ax, [cs:pw_h]
    call pw_putw
    mov al, 0xF7
    call pw_put
    xor al, al
    call pw_put
    call pw_put
    mov si, px_pal
    mov cx, 768
.p:
    lodsb
    call pw_put
    loop .p
    mov al, 0x2C
    call pw_put
    xor ax, ax
    call pw_putw
    call pw_putw
    mov ax, [cs:pw_w]
    call pw_putw
    mov ax, [cs:pw_h]
    call pw_putw
    xor al, al
    call pw_put
    mov al, 8
    call pw_put
    mov es, [cs:pw_dseg]            ; the encoder's table
    mov al, 8
    call lzw_enc_init
    xor bp, bp
.r:
    cmp bp, [cs:pw_h]
    jae .end
    mov ax, bp
    call pw_row
    mov cx, [cs:pw_w]
    push bp
    push ds
    mov es, [cs:pw_dseg]
    mov ds, dx
    call lzw_enc_run
    pop ds
    pop bp
    call pw_tick
    inc bp
    jmp short .r
.end:
    mov es, [cs:pw_dseg]
    call lzw_enc_end
    call pw_gflush
    xor al, al
    call pw_put
    mov al, 0x3B
    call pw_put
    ret

; pw_gifb - LZW_PUTB: a code byte into the sub-block. Preserves all
pw_gifb:
    push bx
    mov bl, [cs:pw_gbn]
    xor bh, bh
    mov [cs:pw_gbuf + bx], al
    inc bl
    mov [cs:pw_gbn], bl
    cmp bl, 255
    jb .o
    call pw_gflush
.o:
    pop bx
    ret

; pw_gflush - the sub-block out: its length, its bytes. Preserves all
pw_gflush:
    push ax
    push cx
    push si
    mov al, [cs:pw_gbn]
    or al, al
    jz .o
    call pw_put
    mov cl, al
    xor ch, ch
    mov si, pw_gbuf
    call pw_puts
    mov byte [cs:pw_gbn], 0
.o:
    pop si
    pop cx
    pop ax
    ret

%define LZW_NODECODE
%define LZW_ENCODE
%define LZW_PUTB pw_gifb
%include "os88lzw.inc"

; =============================================================================
; PIX - os8088's own archive, one picture: packed 4bpp in the sixteen, each
; entry's nearest by plain squared distance, the lower on a tie (write_pix)
; =============================================================================
pw_pix:
    mov es, [cs:pw_dseg]            ; the 256 nearest at the scratch's start
    xor di, di
    mov si, px_pal
.e:
    xor bx, bx                      ; BX = the candidate
    mov word [cs:pw_bd], 0xFFFF
    mov word [cs:pw_bd + 2], 0xFFFF
.c:
    push bx
    mov ax, bx
    call pw_x3
    mov bx, ax
    xor cx, cx                      ; CX:BP = the distance
    xor bp, bp
    mov dl, 3
.ch:
    mov al, [si]
    sub al, [cs:pw_ega + bx]
    jnc .a
    neg al
.a:
    mul al
    add bp, ax
    adc cx, 0
    inc si
    inc bx
    dec dl
    jnz .ch
    sub si, 3
    pop bx
    cmp cx, [cs:pw_bd + 2]
    ja .nx
    jb .best
    cmp bp, [cs:pw_bd]
    jae .nx
.best:
    mov [cs:pw_bd], bp
    mov [cs:pw_bd + 2], cx
    mov [es:di], bl
.nx:
    inc bx
    cmp bx, 16
    jb .c
    inc di
    add si, 3
    cmp di, 256
    jb .e
    mov si, pw_s_pix                ; the header and the one entry
    mov cx, 6
    call pw_puts
    mov ax, 1
    call pw_putw
    xor ax, ax
    call pw_putw
    mov ax, 16
    call pw_putw
    mov cx, 4
    call pw_zeros
    mov ax, 1
    call pw_putw
    mov ax, [cs:pw_w]
    call pw_putw
    mov ax, [cs:pw_h]
    call pw_putw
    mov ax, [cs:pw_w]
    inc ax
    shr ax, 1
    call pw_putw
    mov ax, 32
    xor dx, dx
    call pw_putd
    mov cx, 4
    call pw_zeros
    xor bp, bp
.r:
    cmp bp, [cs:pw_h]
    jae .done
    mov ax, bp
    call pw_row
    mov cx, [cs:pw_w]
    push ds
    mov es, [cs:pw_dseg]
    mov ds, dx
    xor bh, bh
.x:
    lodsb
    mov bl, al
    mov ah, [es:bx]                 ; the left pixel's nearest, high
    mov al, 0
    dec cx
    jz .one
    push ax
    lodsb
    mov bl, al
    pop ax
    mov al, [es:bx]
.one:
    shl ah, 1
    shl ah, 1
    shl ah, 1
    shl ah, 1
    or al, ah
    call pw_put
    jcxz .xd
    loop .x
.xd:
    pop ds
    call pw_tick
    inc bp
    jmp short .r
.done:
    ret

; =============================================================================
; PNG - 8-bit: colour type 3 with a 256-entry PLTE, or 0 on a GREY master;
; filter None; one zlib stream (78 01, one fixed-Huffman block, Adler-32)
; cut into IDAT chunks of PNG_IDAT bytes (write_png8)
; =============================================================================
DS_WIN      equ 0                   ; the scratch's regions (its own segment)
DS_HEAD     equ 16384               ; 4,096 words: a hash's last position
DS_PREV     equ 24576               ; 4,096 words: a position's previous
DS_IDAT     equ 32768               ; the chunk being filled
DS_CRC      equ 40960               ; 256 dwords
DS_REV      equ 41984               ; 256 bytes: a byte's bits reversed
DS_END      equ 42240
DF_NONE     equ 0xFFFF
DF_SLIDE    equ 8192
PNG_IDAT    equ 8192

pw_png:
    call pw_tabs                    ; CRC-32's and the reversals'
    mov si, pw_s_sig
    mov cx, 8
    call pw_puts
    mov es, [cs:pw_dseg]            ; IHDR's 13 bytes, staged in the IDAT
                                    ; buffer (nothing is in it yet)
    mov di, DS_IDAT
    xor ax, ax
    stosw
    mov ax, [cs:pw_w]
    xchg al, ah
    stosw
    xor ax, ax
    stosw
    mov ax, [cs:pw_h]
    xchg al, ah
    stosw
    mov al, 8
    stosb
    mov al, 3
    cmp byte [px_cur + PXR_PMODE], PM_GREY
    jne .ct
    xor al, al
.ct:
    mov [cs:pw_ctype], al
    stosb
    xor ax, ax
    stosw
    stosb
    mov si, pw_s_ihdr
    mov dx, [cs:pw_dseg]
    mov bx, DS_IDAT
    mov cx, 13
    call pw_chunk
    cmp byte [cs:pw_ctype], 0
    je .idat
    mov si, pw_s_plte
    mov dx, ds
    mov bx, px_pal
    mov cx, 768
    call pw_chunk
.idat:
    mov word [cs:pw_zn], 0          ; the zlib stream
    mov al, 0x78
    call pw_z
    mov al, 0x01
    call pw_z
    call df_deflate
    mov ax, [cs:df_s2]              ; Adler-32, big-endian
    xchg al, ah
    call pw_z
    xchg al, ah
    call pw_z
    mov ax, [cs:df_s1]
    xchg al, ah
    call pw_z
    xchg al, ah
    call pw_z
    call pw_zflush
    mov si, pw_s_iend
    xor cx, cx
    call pw_chunk
    ret

; pw_tabs - the CRC-32 table (0xEDB88320, reflected) and the bit-reversal
; table, in the scratch. Preserves BP
pw_tabs:
    mov es, [cs:pw_dseg]
    mov di, DS_CRC
    xor bx, bx
.n:
    mov ax, bx
    xor dx, dx
    mov cx, 8
.k:
    shr dx, 1
    rcr ax, 1
    jnc .z
    xor ax, 0x8320
    xor dx, 0xEDB8
.z:
    loop .k
    stosw
    mov ax, dx
    stosw
    inc bx
    cmp bx, 256
    jb .n
    mov di, DS_REV
    xor bx, bx
.r:
    mov al, bl
    mov cx, 8
.rb:
    shr al, 1
    rcl ah, 1
    loop .rb
    mov al, ah
    stosb
    inc bl
    jnz .r
    ret

; pw_chunk - a chunk: CS:SI its four-byte type, DX:BX its data, CX its
; length: the length, the type, the data, CRC-32 of type and data. Preserves
; DS
pw_chunk:
    push ds
    mov ax, cx                      ; the length, big-endian
    push dx
    xor dx, dx
    call pw_putbe
    pop dx
    mov word [cs:pw_crc], 0xFFFF
    mov word [cs:pw_crc + 2], 0xFFFF
    push cx
    mov cx, 4
.t:
    mov al, [cs:si]
    call pw_put
    call pw_crcb
    inc si
    loop .t
    pop cx
    mov ds, dx
    mov si, bx
    jcxz .c
.d:
    lodsb
    call pw_put
    call pw_crcb
    loop .d
.c:
    mov ax, [cs:pw_crc]
    mov dx, [cs:pw_crc + 2]
    not ax
    not dx
    call pw_putbe
    pop ds
    ret

; pw_crcb - AL into [pw_crc]: table[(crc ^ b) & 255] ^ (crc >> 8).
; Preserves all
pw_crcb:
    push ax
    push bx
    push dx
    push es
    mov es, [cs:pw_dseg]
    mov bl, al
    xor bl, [cs:pw_crc]
    xor bh, bh
    shl bx, 1
    shl bx, 1
    mov ax, [cs:pw_crc + 1]
    mov dl, [cs:pw_crc + 3]
    xor dh, dh
    xor ax, [es:bx + DS_CRC]
    xor dx, [es:bx + DS_CRC + 2]
    mov [cs:pw_crc], ax
    mov [cs:pw_crc + 2], dx
    pop es
    pop dx
    pop bx
    pop ax
    ret

; pw_z - AL, the next byte of the zlib stream: into the IDAT buffer, which
; is a chunk when it is full. Preserves all
pw_z:
    push di
    push es
    mov es, [cs:pw_dseg]
    mov di, [cs:pw_zn]
    mov [es:di + DS_IDAT], al
    inc di
    mov [cs:pw_zn], di
    cmp di, PNG_IDAT
    jb .o
    call pw_zflush
.o:
    pop es
    pop di
    ret

; pw_zflush - the IDAT buffer as a chunk, if it holds anything. Preserves all
pw_zflush:
    push ax
    push bx
    push cx
    push dx
    push si
    mov cx, [cs:pw_zn]
    jcxz .o
    mov si, pw_s_idat
    mov dx, [cs:pw_dseg]
    mov bx, DS_IDAT
    call pw_chunk
    mov word [cs:pw_zn], 0
.o:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- DEFLATE (pixelsim's deflate_fixed) ----------------------------------------------
; The input - each row's filter byte (0) then its pixels - streams through a
; 16 KB window; a position is searched once 262 bytes past it are there (or
; the input has ended), so a match's lookahead and every hash it inserts
; read bytes the window holds; at 12 KB the window slides 8 KB down and the
; heads and links with it (an entry older than the window is dropped: it is
; farther than any distance a match may use)

; df_bits - AX = a value, CL = its bits (<= 13): onto the stream, LSB first.
; Preserves all
df_bits:
    push ax
    push bx
    push cx
    push dx
    mov dx, ax
    mov ch, [cs:df_bn]
    xchg cl, ch                     ; CL = held, CH = new
    shl ax, cl
    or ax, [cs:df_bacc]
    mov bx, dx
    mov dl, 16
    sub dl, cl
    xchg cl, dl
    shr bx, cl                      ; the spill past 16
    mov cl, dl
    add cl, ch                      ; CL = held now
.b:
    cmp cl, 8
    jb .r
    call pw_z
    mov al, ah
    mov ah, bl
    mov bl, bh
    xor bh, bh
    sub cl, 8
    jmp short .b
.r:
    mov [cs:df_bacc], ax
    mov [cs:df_bn], cl
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; df_huff - AX = a Huffman code, CL = its length (5, 7, 8 or 9): MSB first,
; so its bits reversed. Preserves all
df_huff:
    push ax
    push bx
    push es
    mov es, [cs:pw_dseg]
    mov bx, ax
    and bx, 0xFF
    mov bl, [es:bx + DS_REV]        ; rev8 of the low byte
    cmp cl, 9
    jne .s
    shl bx, 1                       ; 9: rev8(low) << 1 | bit 8
    shr ah, 1
    adc bx, 0
    mov ax, bx
    jmp short .o
.s:
    mov ax, bx
    mov bl, 8
    sub bl, cl
    xchg cl, bl
    shr ax, cl                      ; n < 8: rev8 >> (8 - n)
    mov cl, bl
.o:
    call df_bits
    pop es
    pop bx
    pop ax
    ret

; df_lit - AX = a literal/length symbol 0..287
df_lit:
    push ax
    push cx
    cmp ax, 144
    jae .a
    add ax, 0x30
    mov cl, 8
    jmp short .o
.a:
    cmp ax, 256
    jae .b
    add ax, 0x190 - 144
    mov cl, 9
    jmp short .o
.b:
    cmp ax, 280
    jae .c
    sub ax, 256
    mov cl, 7
    jmp short .o
.c:
    add ax, 0xC0 - 280
    mov cl, 8
.o:
    call df_huff
    pop cx
    pop ax
    ret

; df_deflate - the whole stream: rows in, one fixed block out
df_deflate:
    mov es, [cs:pw_dseg]            ; every head empty
    mov di, DS_HEAD
    mov ax, DF_NONE
    mov cx, 4096
    rep stosw
    xor ax, ax
    mov [cs:df_bacc], ax
    mov [cs:df_bn], al
    mov [cs:df_pos], ax
    mov [cs:df_fill], ax
    mov [cs:df_done], al
    mov [cs:df_y], ax
    mov word [cs:df_x], 0xFFFF      ; a row starts with its filter byte
    mov [cs:df_s2], ax
    inc ax
    mov [cs:df_s1], ax
    mov ax, 1                       ; BFINAL
    mov cl, 1
    call df_bits
    mov cl, 2                       ; BTYPE 01: fixed
    call df_bits
.loop:
    call df_fillwin
.p:
    mov ax, [cs:df_fill]
    sub ax, [cs:df_pos]
    jz .chk
    cmp byte [cs:df_done], 0
    jne .go
    cmp ax, 262
    jb .chk
.go:
    call df_step
    jmp short .p
.chk:
    cmp byte [cs:df_done], 0
    je .sl
    mov ax, [cs:df_pos]
    cmp ax, [cs:df_fill]
    jae .end
.sl:
    cmp word [cs:df_pos], 12288
    jb .loop
    call df_slide
    jmp short .loop
.end:
    mov ax, 256                     ; the block's end
    call df_lit
    cmp byte [cs:df_bn], 0
    je .o
    mov al, [cs:df_bacc]
    call pw_z
.o:
    ret

; df_fillwin - the window filled to 16 KB from the rows, or to the input's
; end ([df_done]); Adler-32 over every byte; pw_tick at every row's end
df_fillwin:
    mov es, [cs:pw_dseg]
.l:
    cmp word [cs:df_fill], 16384
    jae .o
    cmp byte [cs:df_done], 0
    jne .o
    mov ax, [cs:df_y]
    cmp ax, [cs:pw_h]
    jb .b
    mov byte [cs:df_done], 1
    jmp short .o
.b:
    mov bx, [cs:df_x]
    cmp bx, 0xFFFF
    jne .px
    xor al, al                      ; the filter byte
    mov word [cs:df_x], 0
    jmp short .put
.px:
    call pw_row                     ; DX:SI = the row
    add si, bx
    push ds
    mov ds, dx
    mov al, [si]
    pop ds
    inc bx
    mov [cs:df_x], bx
    cmp bx, [cs:pw_w]
    jb .put
    mov word [cs:df_x], 0xFFFF      ; the row's end
    inc word [cs:df_y]
    call pw_tick
.put:
    mov es, [cs:pw_dseg]
    mov di, [cs:df_fill]
    mov [es:di + DS_WIN], al
    inc word [cs:df_fill]
    xor ah, ah                      ; Adler: s1 += b, s2 += s1, mod 65521
    add ax, [cs:df_s1]
    jc .c1
    cmp ax, 65521
    jb .s1
.c1:
    sub ax, 65521
.s1:
    mov [cs:df_s1], ax
    add ax, [cs:df_s2]
    jc .c2
    cmp ax, 65521
    jb .s2
.c2:
    sub ax, 65521
.s2:
    mov [cs:df_s2], ax
    jmp .l
.o:
    ret

; df_hash - SI = a window position: BX = its hash x 2. Preserves all else
df_hash:
    push ax
    push es
    mov es, [cs:pw_dseg]
    mov bh, [es:si + DS_WIN]        ; (b0 << 8) ^ (b1 << 4) ^ b2, & 4095
    xor bl, bl
    mov al, [es:si + DS_WIN + 1]
    xor ah, ah
    shl ax, 1
    shl ax, 1
    shl ax, 1
    shl ax, 1
    xor bx, ax
    mov al, [es:si + DS_WIN + 2]
    xor ah, ah
    xor bx, ax
    and bx, 4095
    shl bx, 1
    pop es
    pop ax
    ret

; df_insert - SI = a position: into its hash's chain, when its three bytes
; are there. Preserves all
df_insert:
    push ax
    push bx
    push di
    push es
    mov ax, si
    add ax, 2
    cmp ax, [cs:df_fill]
    jae .o
    mov es, [cs:pw_dseg]
    call df_hash
    mov di, si
    and di, 4095
    shl di, 1
    mov ax, [es:bx + DS_HEAD]
    mov [es:di + DS_PREV], ax
    mov [es:bx + DS_HEAD], si
.o:
    pop es
    pop di
    pop bx
    pop ax
    ret

; df_step - the position [df_pos]: its longest match (the nearest on a tie)
; among DF_CHAIN candidates at most 4,095 back, written as a length and a
; distance when it is 3 or more, else the literal
df_step:
    mov si, [cs:df_pos]
    mov word [cs:df_best], 0
    mov ax, si
    add ax, 2
    cmp ax, [cs:df_fill]
    jae .emit
    mov ax, [cs:df_fill]            ; lim = min(258, fill - pos)
    sub ax, si
    cmp ax, 258
    jbe .l
    mov ax, 258
.l:
    mov [cs:df_lim], ax
    call df_hash
    mov es, [cs:pw_dseg]
    mov bx, [es:bx + DS_HEAD]       ; BX = the candidate
    mov byte [cs:df_chain], 8
.c:
    cmp bx, DF_NONE
    je .emit
    mov ax, si
    sub ax, bx
    jbe .emit                       ; (never at or past the position)
    cmp ax, 4095
    ja .emit
    cmp byte [cs:df_chain], 0
    je .emit
    push si                         ; the match's length: repe cmpsb
    push ds
    mov ds, [cs:pw_dseg]
    mov di, si
    mov si, bx
    mov cx, [cs:df_lim]
    repe cmpsb
    je .all
    inc cx                          ; stopped on a difference
.all:
    pop ds
    pop si
    mov ax, [cs:df_lim]
    sub ax, cx                      ; AX = the length
    cmp ax, [cs:df_best]
    jbe .nx
    mov [cs:df_best], ax
    mov dx, si
    sub dx, bx
    mov [cs:df_dist], dx
    cmp ax, 32                      ; good enough, or as long as it can be
    jae .emit
    cmp ax, [cs:df_lim]
    jae .emit
.nx:
    and bx, 4095
    shl bx, 1
    mov bx, [es:bx + DS_PREV]
    dec byte [cs:df_chain]
    jmp short .c
.emit:
    mov ax, [cs:df_best]
    cmp ax, 3
    jae .match
    mov es, [cs:pw_dseg]            ; a literal
    mov al, [es:si + DS_WIN]
    xor ah, ah
    call df_lit
    call df_insert
    inc word [cs:df_pos]
    ret
.match:
    xor bx, bx                      ; the length's code: LBASE[k+1] > len
.lk:
    cmp bx, 28 * 2
    jae .lf
    cmp ax, [cs:df_lbase + bx + 2]
    jb .lf
    inc bx
    inc bx
    jmp short .lk
.lf:
    push ax
    mov ax, bx
    shr ax, 1
    add ax, 257
    call df_lit
    pop ax
    shr bx, 1
    mov cl, [cs:df_lext + bx]
    or cl, cl
    jz .d
    shl bx, 1
    push ax
    sub ax, [cs:df_lbase + bx]
    call df_bits
    pop ax
.d:
    mov ax, [cs:df_dist]            ; the distance's code: DBASE[j+1] > d
    xor bx, bx
.dk:
    cmp bx, 29 * 2
    jae .df
    cmp ax, [cs:df_dbase + bx + 2]
    jb .df
    inc bx
    inc bx
    jmp short .dk
.df:
    push ax
    mov ax, bx
    shr ax, 1
    mov cl, 5
    call df_huff
    pop ax
    shr bx, 1
    mov cl, [cs:df_dext + bx]
    or cl, cl
    jz .ins
    shl bx, 1
    sub ax, [cs:df_dbase + bx]
    call df_bits
.ins:
    mov cx, [cs:df_best]            ; every position the match covers into
.i:                                 ; its chain
    call df_insert
    inc si
    loop .i
    mov [cs:df_pos], si
    ret

; df_slide - the window's top 8 KB down to its bottom; the heads and links
; with it, an entry below the slide dropped
df_slide:
    push ds
    mov ds, [cs:pw_dseg]
    mov es, [cs:pw_dseg]
    mov si, DS_WIN + DF_SLIDE
    mov di, DS_WIN
    mov cx, [cs:df_fill]
    sub cx, DF_SLIDE
    rep movsb
    mov si, DS_HEAD                 ; heads and links: 8,192 words, both
    mov cx, 8192                    ; arrays end to end
.r:
    mov ax, [si]
    cmp ax, DF_SLIDE
    jb .none
    cmp ax, DF_NONE
    je .k
    sub ax, DF_SLIDE
    jmp short .k
.none:
    mov ax, DF_NONE
.k:
    mov [si], ax
    inc si
    inc si
    loop .r
    pop ds
    sub word [cs:df_pos], DF_SLIDE
    sub word [cs:df_fill], DF_SLIDE
    ret

df_lbase:   dw 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35
            dw 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258, 0xFFFF
df_lext:    db 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3
            db 4, 4, 4, 4, 5, 5, 5, 5, 0
df_dbase:   dw 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193
            dw 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145
            dw 8193, 12289, 16385, 24577, 0xFFFF
df_dext:    db 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8
            db 9, 9, 10, 10, 11, 11, 12, 12, 13, 13

pw_s_gif:   db 'GIF87a'
pw_s_pix:   db 'O8PIX', 1
pw_s_sig:   db 0x89, 'PNG', 13, 10, 26, 10
pw_s_ihdr:  db 'IHDR'
pw_s_plte:  db 'PLTE'
pw_s_idat:  db 'IDAT'
pw_s_iend:  db 'IEND'
pw_ega:     db 0x00, 0x00, 0x00, 0x00, 0x00, 0xAA, 0x00, 0xAA, 0x00
            db 0x00, 0xAA, 0xAA, 0xAA, 0x00, 0x00, 0xAA, 0x00, 0xAA
            db 0xAA, 0x55, 0x00, 0xAA, 0xAA, 0xAA, 0x55, 0x55, 0x55
            db 0x55, 0x55, 0xFF, 0x55, 0xFF, 0x55, 0x55, 0xFF, 0xFF
            db 0xFF, 0x55, 0x55, 0xFF, 0x55, 0xFF, 0xFF, 0xFF, 0x55
            db 0xFF, 0xFF, 0xFF

; --- the part's own words ------------------------------------------------------------
pw_sp:      dw 0
pw_x0:      dw 0                    ; the rect written: its corner in the
pw_y0:      dw 0                    ; master and its size
pw_w:       dw 0
pw_h:       dw 0
pw_dseg:    dw 0
pw_ptr:     dw 0, 0                 ; the output: offset, segment
pw_lim:     dw 0
pw_stride:  dw 0
pw_img:     dd 0
pw_crc:     dd 0
pw_bd:      dd 0
pw_zn:      dw 0
pw_ctype:   db 0
pw_gbn:     db 0
df_bn:      db 0
df_done:    db 0
df_chain:   db 0, 0
df_bacc:    dw 0
df_pos:     dw 0
df_fill:    dw 0
df_y:       dw 0
df_x:       dw 0
df_s1:      dw 0
df_s2:      dw 0
df_best:    dw 0
df_dist:    dw 0
df_lim:     dw 0
pw_gbuf:    times 256 db 0

; THE SCRATCH, its own segment: the LZW table (GIF), or the deflate's window,
; heads, links, IDAT buffer and tables (PNG), or a PCX line, or PIX's nearest
; - one format a save, so one region for all of them
    align 16
pw_scr:
    times DS_END db 0
PW_SCRP     equ (pw_scr - $$) >> 4
%if LZW_EKB * 1024 > DS_END
  %error "the LZW table does not fit the scratch"
%endif

; =============================================================================
; THE UI TASK'S HALF (SPEC.md 106.24): Save As's names, the job's start, the
; pump that writes each full slot from W_ONWAKE, the end - the temporary
; file renamed over the target, or deleted and the reason said - and Copy.
; The resident keeps the dialog's completion, "Replace?", "Save changes?"
; and the dispatch; this reaches its routines through pxsvc.inc's gate.
; IT RUNS BESIDE THE WORKER, which is inside this same part encoding: so
; nothing here touches the worker's own words (pw_sp, pw_ptr, df_*) - only
; the package's handshake bytes, as the decode pump's UI half does.
; =============================================================================

pu_ui:
    mov ax, [px_earg]
    mov bl, cl
    and bl, 0x3F
    xor bh, bh
    shl bx, 1
    jmp [cs:pu_tab + bx]
pu_tab:     dw pu_name, pu_done, pu_start, pu_pump, pu_fin, pu_copy

pu_wext:    db 'PNG', 'GIF', 'BMP', 'PCX', 'PIX', 'BMP'
pu_s_saved: db 'Saved ', 0
pu_s_nsfull: db 'Disk full: not saved', 0
pu_s_nswp:  db 'Disk is write-protected', 0
pu_s_nsio:  db 'Not saved: disk error', 0
pu_s_nscan: db 'Not saved', 0
pu_s_repl:  db 'Replace ', 0
pu_s_isro:  db ' is read-only', 0
pu_s_nsro:  db 'Not saved: read-only', 0
pu_s_kept:  db 'Saved as ', 0
pu_s_qm:    db '?', 0
pu_s_copied: db 'Copied to the clipboard', 0
pu_s_mem:   db 'not enough memory', 0
pu_s_sel:   db '; selection ', 0
pu_s_pix:   db '; pixel ', 0
pu_wfmts:   db PXF_PNG, PXF_GIF, PXF_BMP, PXF_PCX, PXF_PIX, PXF_BMP

; pu_cstr / pu_cat - the part's words into the package's (pxedit.asm's
; pv_cstr / pv_cat). Preserve all but SI / DI
pu_cstr:
    push di
    mov di, px_cline
    mov byte [di], 0
    call pu_cat
    mov si, px_cline
    pop di
    ret
pu_cat:
    push ax
    push si
.e:
    cmp byte [di], 0
    je .c
    inc di
    jmp short .e
.c:
    mov al, [cs:si]
    mov [di], al
    or al, al
    jz .x
    inc si
    inc di
    jmp short .c
.x:
    pop si
    pop ax
    ret

; pu_say - CS:SI said in a toast. Preserves all
pu_say:
    push si
    call pu_cstr
    PSV SV_TOAST
    pop si
    ret

; pu_tmp - SI = the temporary's name, in the package's (a file cell reads
; its name through DS). Preserves all but SI
pu_tmp:
    push ax                         ; 'PX' and the INSTANCE's segment in hex:
    push cx                         ; two PiXELs saving into one folder never
    push di                         ; share a temporary (review-w7 F1)
    mov di, px_line
    mov word [di], 'PX'
    add di, 2
    mov ax, [cs:PXP_PKG]
    mov cx, 4
.h:
    push cx
    mov cl, 4
    rol ax, cl
    pop cx
    push ax
    and al, 15
    add al, '0'
    cmp al, '9'
    jbe .d
    add al, 'A' - '9' - 1
.d:
    mov [di], al
    inc di
    pop ax
    loop .h
    mov word [di], '.T'
    mov word [di + 2], 'MP'
    mov byte [di + 4], 0
    mov si, px_line
    pop di
    pop cx
    pop ax
    ret

; pu_name - WV_UNAME: [px_sname] := the picture's name with the chosen
; format's extension (the dialog's default)
pu_name:
    mov si, px_cur + PXR_NAME
    mov di, px_sname
    PSV SV_STRCPY
    mov si, px_sname                ; its extension off: pu_ext puts ours on
.d:
    cmp byte [si], 0
    je .e
    cmp byte [si], '.'
    je .cut
    inc si
    jmp short .d
.cut:
    mov byte [si], 0
.e:
    call pu_ext
    clc
    ret

; pu_ext - [px_sname]'s extension made [px_wfmt]'s, unless it names a format
; PiXEL writes - then that is the format (BMP stays 24-bit when the card
; said so). Preserves all
pu_ext:
    push ax
    push bx
    push cx
    push si
    push di
    mov si, px_sname
.dot:
    mov al, [si]
    or al, al
    jz .add
    cmp al, '.'
    je .ext
    inc si
    jmp short .dot
.ext:
    inc si                          ; SI = the extension: one of ours?
    mov di, pu_wext
    xor bx, bx
.cmp:
    mov ax, [si]
    and ax, 0xDFDF                  ; (either case)
    cmp ax, [cs:di]
    jne .nx
    mov al, [si + 2]
    and al, 0xDF
    cmp al, [cs:di + 2]
    jne .nx
    cmp byte [si + 3], 0
    jne .nx
    cmp bl, WF_BMP                  ; it is: the format
    jne .set
    cmp byte [px_wfmt], WF_BMP24
    je .x
.set:
    mov [px_wfmt], bl
    jmp short .x
.nx:
    add di, 3
    inc bx
    cmp bx, WF_BMP24
    jb .cmp
    dec si                          ; another extension: ours instead
    jmp short .put
.add:
    mov byte [si], '.'
.put:
    inc si
    mov bl, [px_wfmt]
    xor bh, bh
    mov ax, bx
    shl bx, 1
    add bx, ax
    mov ax, [cs:pu_wext + bx]
    mov [si], ax
    mov al, [cs:pu_wext + bx + 2]
    mov [si + 2], al
    mov byte [si + 3], 0
.x:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; pu_done - WV_UDONE: the dialog answered [px_sname] (the resident copied
; it): its extension, the folder it goes to, and - CF = 1 - "Replace
; NAME?" in [px_aq] when the name is taken
pu_done:
    push ds                         ; (OSAPI_FILE_FIND fills ES:DI: ours)
    pop es
    call pu_ext
    call OSAPI_FILE_HERE
    mov [px_sdir], dx
    mov [px_svol], bl
    xor cx, cx                      ; the folder's names: is it one?
.n:
    mov di, px_find
    call OSAPI_FILE_FIND
    jc .new
    mov si, px_find
    mov di, px_sname
    PSV SV_STRCMP
    jne .n
    mov di, px_aq
    mov byte [di], 0
    cmp word [px_find + 14], OSAPI_FT_DIR   ; A FOLDER, or a READ-ONLY file:
    jae .ro                         ; refused now, not after the whole
    test byte [px_find + 13], 1     ; encode (review-w7 F4)
    jz .rq
.ro:
    mov si, px_sname
    mov di, px_cline
    PSV SV_STRCPY
    mov si, pu_s_isro
    call pu_cat
    mov si, px_cline
    PSV SV_TOAST
    stc                             ; (CF and no question: the resident
    ret                             ; says nothing more)
.rq:
    mov si, pu_s_repl
    call pu_cat
    mov si, px_sname
    PSV SV_STRCAT
    mov si, pu_s_qm
    call pu_cat
    stc
    ret
.new:
    clc
    ret

; pu_start - WV_USTART: the ring claimed for the volume the save goes to
; (its slots a multiple of its cluster) - a copy's one slot, the whole BMP -
; the handshake's bytes cleared, the worker told LAST. CF = 1 no memory
; (nothing held then)
pu_start:
    mov byte [px_clok], 0           ; the target volume's cluster
    PSV SV_RINGCL
    jc .no
    PSV SV_SPAWN
    jc .nw
    xor ax, ax
    mov [px_sfull], ax
    mov [px_fslot], al
    mov [px_wslot], al
    mov [px_rq], al
    mov [px_rerr], al
    mov [px_abort], al
    mov [px_wtok], ax
    mov [px_wtot], ax
    mov [px_wtot + 2], ax
    mov [px_erow], ax
    mov byte [px_wfirst], 1
    mov ax, [px_cur + PXR_MH]
    mov [px_erows], ax
    mov byte [px_busy], PXB_SAVE
    mov byte [px_job], JOB_SAVE     ; LAST
    PSV SV_EPROG
    PSV SV_FLAGS
    PSV SV_UPDATE
    clc
    ret
.nw:
    PSV SV_WFREE
.no:
    stc
    ret

; pu_goto - stand in the folder the save goes to. CF = 1 it is gone (a disk
; taken out). Preserves all
pu_goto:
    push ax
    push bx
    push dx
    call OSAPI_FILE_HERE
    cmp dx, [px_sdir]
    jne .go
    cmp bl, [px_svol]
    je .ok
.go:
    mov dx, [px_sdir]
    mov bl, [px_svol]
    call OSAPI_FILE_GOTO
    jc .x
.ok:
    clc
.x:
    pop dx
    pop bx
    pop ax
    ret

; pu_pump - WV_UPUMP: W_ONWAKE, UI TASK, NO LOCK: every slot the worker has
; filled, in order, written to the temporary file - the first creates it
; (OSAPI_FILE_WRITE), the rest go on with OSAPI_FILE_WRITE_SEQ, held
; (SPEC.md 18.4.9) - and emptied; the request byte cleared LAST. A refused
; write is the worker's cancel, its number kept for the toast
pu_pump:
.s:
    mov bl, [px_fslot]
    xor bh, bh
    cmp byte [px_sfull + bx], 0
    je .rq
    cmp byte [px_rerr], 0
    jne .drop
    call pu_goto
    jc .gone
    shl bx, 1
    mov cx, [px_slen + bx]
    mov es, [px_sseg + bx]
    xor bx, bx
    call pu_tmp
    cmp byte [px_wfirst], 0
    je .seq
    mov byte [px_wfirst], 0
    xor dx, dx
    call OSAPI_FILE_WRITE
    jmp short .w
.seq:
    mov al, WSEQF_HELD
    mov di, [px_wtok]
    call OSAPI_FILE_WRITE_SEQ
    mov [px_wtok], di
.w:
    jnc .ok
    mov [px_rerr], al
    mov byte [px_abort], 1          ; the worker stops at its next slot
    jmp short .drop
.gone:
    mov byte [px_rerr], FERR_NODISK
    mov byte [px_abort], 1
    jmp short .drop
.ok:
    add [px_wtot], cx
    adc word [px_wtot + 2], 0
.drop:
    mov bl, [px_fslot]
    xor bh, bh
    mov byte [px_sfull + bx], 0
    inc bl
    cmp bl, [px_nslot]
    jb .n
    xor bl, bl
.n:
    mov [px_fslot], bl
    jmp .s
.rq:
    mov byte [px_rq], 0             ; LAST
    clc
    ret

; pu_fin - WV_UFIN: THE WORKER HAS ANSWERED a save (UI task, lock held,
; [px_busy] already 0): the stream closed, the target replaced by the
; temporary - or the temporary deleted and the reason said
pu_fin:
    call pu_pump                    ; THE LAST SLOT: the worker can fill it
                                    ; and answer between W_ONWAKE's pump and
                                    ; its look at [px_job] (a PCX lost its
                                    ; final 1,325 bytes so)
    PSV SV_WFREE
    cmp byte [px_wres], 0           ; the worker's answer, or the disk's
    jne .fail
    cmp byte [px_rerr], 0
    jne .fail
    call pu_goto
    jc .fail
    call pu_tmp
    cmp word [px_wtok], 0           ; the held stream's close
    je .del
    xor cx, cx
    mov al, WSEQF_HELD
    mov di, [px_wtok]
    call OSAPI_FILE_WRITE_SEQ
    jc .wfail
.del:
    mov si, px_sname                ; the target goes (it may not be there)
    call OSAPI_FILE_DELETE
    jnc .ren
    cmp ax, FERR_NOENT
    jne .wfail
.ren:
    call pu_tmp
    mov di, px_sname
    call OSAPI_FILE_RENAME
    jnc .saved
    call pu_tmp                     ; THE TARGET IS GONE and the temporary
    mov di, px_cline                ; holds the picture: kept, and named in
    mov byte [di], 0                ; the toast - never deleted (review-w7
    push si                         ; F3)
    mov si, pu_s_kept
    call pu_cat
    pop si
    PSV SV_STRCAT
    mov si, px_cline
    PSV SV_TOAST
    jmp .nod2
.saved:
    ; --- SAVED: the picture is the file's now ---------------------------------
    mov si, px_sname
    mov di, px_cur + PXR_NAME
    PSV SV_STRCPY
    mov ax, [px_sdir]
    mov [px_cur + PXR_DIR], ax
    mov al, [px_svol]
    mov [px_cur + PXR_VOL], al
    mov bl, [px_wfmt]
    xor bh, bh
    mov al, [cs:pu_wfmts + bx]
    mov [px_cur + PXR_FMT], al
    mov ax, [px_wtot]
    mov [px_cur + PXR_FSIZE], ax
    mov ax, [px_wtot + 2]
    mov [px_cur + PXR_FSIZE + 2], ax
    mov ax, [px_cur + PXR_MW]       ; ...and the file IS the master now: its
    mov [px_cur + PXR_SW], ax       ; size, at 1/1, 8 bits a pixel (24 for
    mov ax, [px_cur + PXR_MH]       ; BMP 24) - review-w7 F6
    mov [px_cur + PXR_SH], ax
    xor ax, ax
    mov [px_cur + PXR_SCL], al
    mov [px_cur + PXR_FAST], al
    mov [px_cur + PXR_DSCL], al
    mov al, 8
    cmp byte [px_wfmt], WF_BMP24
    jne .b8
    mov al, 24
.b8:
    mov [px_cur + PXR_BITS], al
    mov byte [px_dirty], 0
    mov byte [px_udirty], 1         ; (undo now leaves it unsaved)
    PSV SV_WALK                     ; the folder as it is now
    PSV SV_COMPOSE
    PSV SV_FLAGS
    mov bx, [px_win]                ; "PiXEL - NAME.EXT": a strip
    mov ax, px_title
    call OSAPI_WM_TITLE
    mov al, PANELS_FS_STATUS
    PSV SV_REGDRAW
    mov si, pu_s_saved              ; "Saved NAME.EXT"
    call pu_cstr
    mov di, px_cline
    mov si, px_sname
    PSV SV_STRCAT
    mov si, px_cline
    PSV SV_TOAST
    PSV SV_KKEEP                    ; (BEFORE the banked action: a Next it
    PSV SV_LVDO                     ; starts has a decode running in its part,
    clc                             ; and px_kkeep with nothing HAVE would
    ret                             ; drop it under the worker - "damaged")
.wfail:
    mov [px_rerr], al
.fail:
    call pu_goto                    ; the temporary goes
    jc .nod
    call pu_tmp
    call OSAPI_FILE_DELETE
.nod:
    mov si, pu_s_nscan              ; ...and why, in a toast
    mov al, [px_rerr]
    or al, al
    jz .say
    mov si, pu_s_nsfull
    cmp al, FERR_FULL
    je .say
    cmp al, FERR_DIRFULL
    je .say
    mov si, pu_s_nswp
    cmp al, FERR_WPROT
    je .say
    mov si, pu_s_nsro
    cmp al, FERR_PROT
    je .say
    mov si, pu_s_nsio
.say:
    call pu_say
.nod2:
    mov byte [px_lvpend], 0         ; a question that asked to save: dropped
    PSV SV_COMPOSE
    PSV SV_FLAGS
    mov al, PANELS_FS_STATUS
    PSV SV_REGDRAW
.end:
    PSV SV_KKEEP
    clc
    ret
PANELS_FS_STATUS equ 8 | 16 | 32    ; pixel.asm's PX_R_PANELS | _FS | _STATUS

; pu_copy - WV_UCOPY: Edit > Copy Info. THE CLIPBOARD IS TEXT (SPEC.md
; 55.1: "a bitmap pasted into Note Pad has no meaning"), so what goes on it is
; the picture DESCRIBED - "CITY.PCX 320x240 PCX", then the selection's master
; rect and the Eyedropper's pinned pixel when there are ones (SPEC.md
; 106.25) - one line, built in this part's own memory and put with ES = CS.
; Said in a toast either way
pu_copy:
    push es
    push cs
    pop es
    mov di, pu_ctext                ; ES:DI, the line
    mov si, px_ival                 ; Image Info's File: the name
    call pu_tds
    mov al, ' '
    stosb
    mov ax, [px_cur + PXR_SW]       ; the source's size
    call pu_tnum
    mov al, 'x'
    stosb
    mov ax, [px_cur + PXR_SH]
    call pu_tnum
    mov al, ' '
    stosb
    mov si, px_ival + 3 * PE_IVSZ   ; ...and Format's value
    call pu_tds
    cmp byte [px_sel], 0
    je .pk
    mov si, pu_s_sel                ; "; selection 10,20-109,119"
    call pu_tcs
    mov si, px_selr
    call pu_txy
    mov al, '-'
    stosb
    mov si, px_selr + 4
    call pu_txy
.pk:
    mov al, [px_ival + PE_IPICK * PE_IVSZ]
    or al, al                       ; (none pinned: empty, or the line's
    jz .put                         ; own "-")
    cmp al, '-'
    je .put
    mov si, pu_s_pix                ; "; pixel 123,45 #A0B0C0 17"
    call pu_tcs
    mov si, px_ival + PE_IPICK * PE_IVSZ
    call pu_tds
.put:
    mov cx, di
    sub cx, pu_ctext
    mov si, pu_ctext
    call OSAPI_CLIP_PUT             ; ES:SI, CX
    pop es
    mov si, pu_s_copied
    jnc .say
    mov si, pu_s_mem
.say:
    call pu_say
    clc
    ret

; pu_tds / pu_tcs - the NUL string at DS:SI (the package's) or CS:SI (this
; part's) onto ES:DI, its NUL not. Clobber AL, SI
pu_tds:
    lodsb
    or al, al
    jz .x
    stosb
    jmp short pu_tds
.x:
    ret
pu_tcs:
    mov al, [cs:si]
    inc si
    or al, al
    jz .x
    stosb
    jmp short pu_tcs
.x:
    ret

; pu_txy - "x,y" of the two words at DS:SI onto ES:DI. Clobbers AX, SI
pu_txy:
    lodsw
    call pu_tnum
    mov al, ','
    stosb
    lodsw
    ; fall into pu_tnum

; pu_tnum - AX in decimal onto ES:DI, its digits made backwards in
; pu_tdig (a loop that pushes is one the stack checker cannot follow).
; Clobbers AX
pu_tnum:
    push bx
    push dx
    push si
    mov si, pu_tdig + 5
    mov bx, 10
.d:
    xor dx, dx
    div bx
    add dl, '0'
    dec si
    mov [cs:si], dl
    or ax, ax
    jnz .d
.p:
    mov al, [cs:si]
    stosb
    inc si
    cmp si, pu_tdig + 5
    jb .p
    pop si
    pop dx
    pop bx
    ret

pu_tdig:    times 5 db 0
pu_ctext:   times 128 db 0          ; the line, before it is put

pv_sv:      dw 0, 0                 ; the resident's service gate (pxsvc.inc)
